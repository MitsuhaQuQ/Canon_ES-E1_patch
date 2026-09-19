#define WIN32_LEAN_AND_MEAN
#define _WIN32_WINNT 0x0601
#include <windows.h>
#include <setupapi.h>
#include <winusb.h>
#include <winreg.h>

#define EOS_VIDPID "vid_04a9&pid_3040"
#define EOS_PIPE_IN  0x81
#define EOS_PIPE_OUT 0x02
#define EOS_BLOCK_SIZE 64
#define EOS_MAX_PAYLOAD 62
#define RX_CAPACITY 8192

static const GUID eos_usb_interface =
    {0x290DECB5,0x2458,0x41EC,{0x87,0xA8,0x4D,0x09,0xBD,0xD2,0x0C,0xE2}};
static const GUID generic_usb_interface =
    {0xA5DCBF10,0x6530,0x11D2,{0x90,0x1F,0x00,0xC0,0x4F,0xB9,0x51,0xED}};

static HMODULE g_module;
static BOOL g_is_memory_process;
static char g_module_directory[MAX_PATH];
static HANDLE g_log = INVALID_HANDLE_VALUE;
static HANDLE g_comm = INVALID_HANDLE_VALUE;
static HANDLE g_device = INVALID_HANDLE_VALUE;
static WINUSB_INTERFACE_HANDLE g_usb;
static CRITICAL_SECTION g_state_lock;
static CRITICAL_SECTION g_log_lock;
static COMMTIMEOUTS g_timeouts;
static DCB g_dcb;
static LONG g_exit_sent;
static BOOL g_had_session;
static BYTE g_rx[RX_CAPACITY];
static DWORD g_rx_head;
static DWORD g_rx_count;
static char g_line[8192];
static const HKEY g_fake_serial_key=(HKEY)(ULONG_PTR)0xE0510001;
static void log_line(const char *kind,HANDLE h,const BYTE *data,DWORD size,DWORD requested,DWORD result,DWORD error);

typedef void *(__cdecl *PF_FOPEN)(const char *,const char *);
typedef size_t (__cdecl *PF_FWRITE)(const void *,size_t,size_t,void *);
typedef int (__cdecl *PF_FCLOSE)(void *);
static PF_FOPEN g_real_fopen;
static PF_FWRITE g_real_fwrite;
static PF_FCLOSE g_real_fclose;

static char *put_text(char *p,const char *s){while(*s)*p++=*s++;return p;}
static char *put_hex_nibble(char *p,unsigned v){*p++="0123456789ABCDEF"[v&15];return p;}
static char *put_hex_byte(char *p,unsigned v){p=put_hex_nibble(p,v>>4);return put_hex_nibble(p,v);}
static char *put_hex32(char *p,DWORD v){int n;p=put_text(p,"0x");for(n=7;n>=0;--n)p=put_hex_nibble(p,v>>(n*4));return p;}
static char *put_dec(char *p,DWORD v){char t[16];int n=0;if(!v){*p++='0';return p;}while(v){t[n++]=(char)('0'+v%10);v/=10;}while(n)*p++=t[--n];return p;}

static int lower_ascii(int c){return c>='A'&&c<='Z'?c+('a'-'A'):c;}
static int contains_i(const char *text,const char *part){
    const char *a,*b;if(!text||!part)return 0;
    for(;*text;++text){a=text;b=part;while(*a&&*b&&lower_ascii((unsigned char)*a)==lower_ascii((unsigned char)*b)){++a;++b;}if(!*b)return 1;}
    return 0;
}
static int is_com_path(const char *s){
    const char *p;if(!s)return 0;p=s;
    if(p[0]=='\\'&&p[1]=='\\'&&p[2]=='.'&&p[3]=='\\')p+=4;
    if(lower_ascii((unsigned char)p[0])!='c'||lower_ascii((unsigned char)p[1])!='o'||lower_ascii((unsigned char)p[2])!='m')return 0;
    p+=3;if(*p<'0'||*p>'9')return 0;while(*p>='0'&&*p<='9')++p;return *p==0;
}
static int equals_i(const char *a,const char *b){if(!a||!b)return 0;while(*a&&*b){if(lower_ascii((unsigned char)*a)!=lower_ascii((unsigned char)*b))return 0;++a;++b;}return *a==0&&*b==0;}
static int starts_i(const char *text,const char *prefix){if(!text||!prefix)return 0;while(*prefix){if(!*text||lower_ascii((unsigned char)*text)!=lower_ascii((unsigned char)*prefix))return 0;++text;++prefix;}return 1;}
static const char *find_i(const char *text,const char *part){
    const char *a,*b;if(!text||!part)return 0;
    for(;*text;++text){a=text;b=part;while(*a&&*b&&lower_ascii((unsigned char)*a)==lower_ascii((unsigned char)*b)){++a;++b;}if(!*b)return text;}return 0;
}
static int main_executable_is(const char *name){char path[MAX_PATH],*p,*base=path;DWORD n=GetModuleFileNameA(0,path,MAX_PATH);if(!n||n>=MAX_PATH)return 0;for(p=path;*p;++p)if(*p=='\\'||*p=='/')base=p+1;return equals_i(base,name);}
static LPCSTR fix_application_path(LPCSTR path,char fixed[MAX_PATH]){
    const char *tail=0;DWORD directory_chars,path_chars;
    if(!path||!g_module_directory[0])return path;
    if(path[0]=='\\'||path[0]=='/'){
        if(starts_i(path,"\\DATA\\")||starts_i(path,"/DATA/")||equals_i(path,"\\DATA")||equals_i(path,"/DATA")||(g_is_memory_process&&(equals_i(path,"\\FilmCach.sav")||equals_i(path,"/FilmCach.sav"))))tail=path;
    }else if(path[0]&&path[1]==':'&&(path[2]=='\\'||path[2]=='/')){
        if(starts_i(path+2,"\\DATA\\")||starts_i(path+2,"/DATA/")||equals_i(path+2,"\\DATA")||equals_i(path+2,"/DATA")||(g_is_memory_process&&(equals_i(path+2,"\\FilmCach.sav")||equals_i(path+2,"/FilmCach.sav"))))tail=path+2;
    }
    if(!tail)return path;directory_chars=(DWORD)lstrlenA(g_module_directory);path_chars=(DWORD)lstrlenA(tail);if(directory_chars+path_chars>=MAX_PATH)return path;
    CopyMemory(fixed,g_module_directory,directory_chars);CopyMemory(fixed+directory_chars,tail,path_chars+1);return fixed;
}
static void ensure_log(void){
    char path[MAX_PATH];DWORD n;char *slash=0,*p;
    if(g_log!=INVALID_HANDLE_VALUE)return;
    n=GetModuleFileNameA(g_module,path,MAX_PATH-20);if(!n||n>=MAX_PATH-20)return;
    for(p=path;*p;++p)if(*p=='\\'||*p=='/')slash=p;
    p=slash?slash+1:path;p=put_text(p,"EOSBRIDGE.LOG");*p=0;
    g_log=CreateFileA(path,GENERIC_WRITE,FILE_SHARE_READ|FILE_SHARE_WRITE,0,OPEN_ALWAYS,FILE_ATTRIBUTE_NORMAL,0);if(g_log!=INVALID_HANDLE_VALUE)SetFilePointer(g_log,0,0,FILE_END);
}
static void set_module_directory(void){
    char path[MAX_PATH],*p,*slash=0;DWORD n=GetModuleFileNameA(g_module,path,MAX_PATH);
    if(!n||n>=MAX_PATH)return;for(p=path;*p;++p)if(*p=='\\'||*p=='/')slash=p;if(!slash)return;*slash=0;lstrcpyA(g_module_directory,path);SetCurrentDirectoryA(path);
}
static void log_line(const char *kind,HANDLE h,const BYTE *data,DWORD size,DWORD requested,DWORD result,DWORD error){
    char *p=g_line;DWORD written,i,limit=size;
    EnterCriticalSection(&g_log_lock);ensure_log();
    if(g_log==INVALID_HANDLE_VALUE){LeaveCriticalSection(&g_log_lock);return;}
    p=put_dec(p,GetTickCount());p=put_text(p," pid=");p=put_dec(p,GetCurrentProcessId());*p++=' ';p=put_text(p,kind);p=put_text(p," h=");p=put_hex32(p,(DWORD)(ULONG_PTR)h);
    p=put_text(p," result=");p=put_dec(p,result);p=put_text(p," requested=");p=put_dec(p,requested);p=put_text(p," size=");p=put_dec(p,size);
    p=put_text(p," error=");p=put_dec(p,error);
    if(data&&size){p=put_text(p," data=");if(limit>2048)limit=2048;for(i=0;i<limit&&p<g_line+sizeof(g_line)-5;++i){p=put_hex_byte(p,data[i]);*p++=' ';}if(limit<size)p=put_text(p,"...");}
    *p++='\r';*p++='\n';WriteFile(g_log,g_line,(DWORD)(p-g_line),&written,0);FlushFileBuffers(g_log);
    LeaveCriticalSection(&g_log_lock);
}
static void log_memory_redirect(LPCSTR original,LPCSTR effective){if(original&&effective&&effective!=original)log_line("APPLICATION_PATH_REDIRECT",0,(const BYTE*)effective,(DWORD)lstrlenA(effective),original?(DWORD)lstrlenA(original):0,TRUE,0);}

HFILE WINAPI Trace_lcreat(LPCSTR path,int attr){
    char fixed[MAX_PATH];LPCSTR effective=fix_application_path(path,fixed);HFILE h;DWORD error;SetLastError(ERROR_SUCCESS);h=_lcreat(effective,attr);error=GetLastError();log_memory_redirect(path,effective);
    log_line("FILE_LCREAT",(HANDLE)(ULONG_PTR)h,(const BYTE*)path,path?(DWORD)lstrlenA(path):0,(DWORD)attr,h!=HFILE_ERROR,error);return h;
}
HFILE WINAPI Trace_lopen(LPCSTR path,int mode){
    char fixed[MAX_PATH];LPCSTR effective=fix_application_path(path,fixed);HFILE h;DWORD error;SetLastError(ERROR_SUCCESS);h=_lopen(effective,mode);error=GetLastError();log_memory_redirect(path,effective);
    log_line("FILE_LOPEN",(HANDLE)(ULONG_PTR)h,(const BYTE*)path,path?(DWORD)lstrlenA(path):0,(DWORD)mode,h!=HFILE_ERROR,error);return h;
}
UINT WINAPI Trace_lwrite(HFILE h,LPCCH data,UINT size){
    UINT done;DWORD error;SetLastError(ERROR_SUCCESS);done=_lwrite(h,data,size);error=GetLastError();
    log_line("FILE_LWRITE",(HANDLE)(ULONG_PTR)h,0,done,size,done!=HFILE_ERROR,error);return done;
}
HFILE WINAPI Trace_lclose(HFILE h){
    HFILE result;DWORD error;SetLastError(ERROR_SUCCESS);result=_lclose(h);error=GetLastError();
    log_line("FILE_LCLOSE",(HANDLE)(ULONG_PTR)h,0,0,0,result!=HFILE_ERROR,error);return result;
}
BOOL WINAPI Trace_CreateDirectoryA(LPCSTR path,LPSECURITY_ATTRIBUTES sa){
    char fixed[MAX_PATH];LPCSTR effective=fix_application_path(path,fixed);BOOL ok;DWORD error;SetLastError(ERROR_SUCCESS);ok=CreateDirectoryA(effective,sa);error=GetLastError();log_memory_redirect(path,effective);
    log_line("FILE_MKDIR",0,(const BYTE*)path,path?(DWORD)lstrlenA(path):0,0,ok,error);return ok;
}
BOOL WINAPI Trace_MoveFileA(LPCSTR old_path,LPCSTR new_path){
    char fixed_old[MAX_PATH],fixed_new[MAX_PATH];LPCSTR effective_old=fix_application_path(old_path,fixed_old),effective_new=fix_application_path(new_path,fixed_new);BOOL ok;DWORD error;SetLastError(ERROR_SUCCESS);ok=MoveFileA(effective_old,effective_new);error=GetLastError();log_memory_redirect(old_path,effective_old);log_memory_redirect(new_path,effective_new);
    log_line("FILE_MOVE_FROM",0,(const BYTE*)old_path,old_path?(DWORD)lstrlenA(old_path):0,0,ok,error);
    log_line("FILE_MOVE_TO",0,(const BYTE*)new_path,new_path?(DWORD)lstrlenA(new_path):0,0,ok,error);return ok;
}
BOOL WINAPI Trace_DeleteFileA(LPCSTR path){
    char fixed[MAX_PATH];LPCSTR effective=fix_application_path(path,fixed);BOOL ok;DWORD error;SetLastError(ERROR_SUCCESS);ok=DeleteFileA(effective);error=GetLastError();log_memory_redirect(path,effective);
    log_line("FILE_DELETE",0,(const BYTE*)path,path?(DWORD)lstrlenA(path):0,0,ok,error);return ok;
}
UINT WINAPI Trace_GetTempFileNameA(LPCSTR path,LPCSTR prefix,UINT unique,LPSTR output){
    UINT result;DWORD error;char fixed[MAX_PATH];LPCSTR effective=path;
    effective=fix_application_path(path,fixed);
    SetLastError(ERROR_SUCCESS);result=GetTempFileNameA(effective,prefix,unique,output);error=GetLastError();
    log_line("FILE_TEMP_PATH",0,(const BYTE*)path,path?(DWORD)lstrlenA(path):0,unique,result,error);
    log_line("FILE_TEMP_PREFIX",0,(const BYTE*)prefix,prefix?(DWORD)lstrlenA(prefix):0,unique,result,error);
    if(effective!=path)log_line("FILE_TEMP_REDIRECT",0,(const BYTE*)effective,(DWORD)lstrlenA(effective),unique,result,error);
    log_line("FILE_TEMP_RESULT",0,(const BYTE*)output,output?(DWORD)lstrlenA(output):0,unique,result,error);return result;
}
static UINT WINAPI Trace_WinExec(LPCSTR command,UINT show){
    static const char legacy_memory[]="\\Memory.exe";char fixed[1024],module[MAX_PATH],*p,*slash=0;LPCSTR effective=command,memory=find_i(command,legacy_memory);UINT result;DWORD error;
    if(memory){
        DWORD n=GetModuleFileNameA(g_module,module,MAX_PATH);if(n&&n<MAX_PATH){
            for(p=module;*p;++p)if(*p=='\\'||*p=='/')slash=p;
            if(slash){p=fixed;*p++='"';CopyMemory(p,module,(SIZE_T)(slash+1-module));p+=(slash+1-module);p=put_text(p,"Memory.exe\"");p=put_text(p,memory+sizeof(legacy_memory)-1);*p=0;effective=fixed;}
        }
    }
    SetLastError(ERROR_SUCCESS);result=WinExec(effective,show);error=GetLastError();
    log_line("EXEC_COMMAND",0,(const BYTE*)command,command?(DWORD)lstrlenA(command):0,show,result,error);
    if(effective!=command)log_line("EXEC_REDIRECT",0,(const BYTE*)effective,(DWORD)lstrlenA(effective),show,result,error);return result;
}
static void *__cdecl Trace_fopen(const char *path,const char *mode){
    char fixed[MAX_PATH];const char *effective=fix_application_path(path,fixed);void *f;DWORD error;if(!g_real_fopen)return 0;SetLastError(ERROR_SUCCESS);f=g_real_fopen(effective,mode);error=GetLastError();log_memory_redirect(path,effective);
    log_line("FILE_FOPEN",(HANDLE)f,(const BYTE*)path,path?(DWORD)lstrlenA(path):0,mode?(DWORD)(BYTE)mode[0]:0,f!=0,error);return f;
}
static size_t __cdecl Trace_fwrite(const void *data,size_t size,size_t count,void *f){
    size_t done;DWORD error;if(!g_real_fwrite)return 0;SetLastError(ERROR_SUCCESS);done=g_real_fwrite(data,size,count,f);error=GetLastError();
    log_line("FILE_FWRITE",(HANDLE)f,0,(DWORD)done,(DWORD)count,done==count,error);return done;
}
static int __cdecl Trace_fclose(void *f){
    int result;DWORD error;if(!g_real_fclose)return -1;SetLastError(ERROR_SUCCESS);result=g_real_fclose(f);error=GetLastError();
    log_line("FILE_FCLOSE",(HANDLE)f,0,0,0,result==0,error);return result;
}
BOOL WINAPI Trace_CopyFileA(LPCSTR old_path,LPCSTR new_path,BOOL fail_if_exists){char fixed_old[MAX_PATH],fixed_new[MAX_PATH];LPCSTR effective_old=fix_application_path(old_path,fixed_old),effective_new=fix_application_path(new_path,fixed_new);BOOL ok;DWORD error;SetLastError(ERROR_SUCCESS);ok=CopyFileA(effective_old,effective_new,fail_if_exists);error=GetLastError();log_memory_redirect(old_path,effective_old);log_memory_redirect(new_path,effective_new);log_line("FILE_COPY_FROM",0,(const BYTE*)old_path,old_path?(DWORD)lstrlenA(old_path):0,0,ok,error);log_line("FILE_COPY_TO",0,(const BYTE*)new_path,new_path?(DWORD)lstrlenA(new_path):0,0,ok,error);return ok;}
BOOL WINAPI Trace_SetFileAttributesA(LPCSTR path,DWORD attributes){char fixed[MAX_PATH];LPCSTR effective=fix_application_path(path,fixed);BOOL ok;DWORD error;SetLastError(ERROR_SUCCESS);ok=SetFileAttributesA(effective,attributes);error=GetLastError();log_memory_redirect(path,effective);log_line("FILE_ATTRIBUTES",0,(const BYTE*)path,path?(DWORD)lstrlenA(path):0,attributes,ok,error);return ok;}
BOOL WINAPI Trace_RemoveDirectoryA(LPCSTR path){char fixed[MAX_PATH];LPCSTR effective=fix_application_path(path,fixed);BOOL ok;DWORD error;SetLastError(ERROR_SUCCESS);ok=RemoveDirectoryA(effective);error=GetLastError();log_memory_redirect(path,effective);log_line("FILE_RMDIR",0,(const BYTE*)path,path?(DWORD)lstrlenA(path):0,0,ok,error);return ok;}
HANDLE WINAPI Trace_FindFirstFileA(LPCSTR path,LPWIN32_FIND_DATAA data){char fixed[MAX_PATH];LPCSTR effective=fix_application_path(path,fixed);HANDLE h;DWORD error;SetLastError(ERROR_SUCCESS);h=FindFirstFileA(effective,data);error=GetLastError();log_memory_redirect(path,effective);log_line("FILE_FIND_FIRST",h,(const BYTE*)path,path?(DWORD)lstrlenA(path):0,0,h!=INVALID_HANDLE_VALUE,error);return h;}
static VOID WINAPI Trace_PostQuitMessage(int exit_code);

static FARPROC trace_target(const char *name){
    if(equals_i(name,"_lcreat"))return (FARPROC)Trace_lcreat;
    if(equals_i(name,"_lopen"))return (FARPROC)Trace_lopen;
    if(equals_i(name,"_lwrite"))return (FARPROC)Trace_lwrite;
    if(equals_i(name,"_lclose"))return (FARPROC)Trace_lclose;
    if(equals_i(name,"CreateDirectoryA"))return (FARPROC)Trace_CreateDirectoryA;
    if(equals_i(name,"MoveFileA"))return (FARPROC)Trace_MoveFileA;
    if(equals_i(name,"DeleteFileA"))return (FARPROC)Trace_DeleteFileA;
    if(equals_i(name,"GetTempFileNameA"))return (FARPROC)Trace_GetTempFileNameA;
    if(equals_i(name,"WinExec"))return (FARPROC)Trace_WinExec;
    if(equals_i(name,"fopen"))return (FARPROC)Trace_fopen;
    if(equals_i(name,"fwrite"))return (FARPROC)Trace_fwrite;
    if(equals_i(name,"fclose"))return (FARPROC)Trace_fclose;
    if(equals_i(name,"CopyFileA"))return (FARPROC)Trace_CopyFileA;
    if(equals_i(name,"SetFileAttributesA"))return (FARPROC)Trace_SetFileAttributesA;
    if(equals_i(name,"RemoveDirectoryA"))return (FARPROC)Trace_RemoveDirectoryA;
    if(equals_i(name,"FindFirstFileA"))return (FARPROC)Trace_FindFirstFileA;
    if(equals_i(name,"PostQuitMessage"))return (FARPROC)Trace_PostQuitMessage;
    return 0;
}
static void patch_main_file_iat(void){
    BYTE *base=(BYTE*)GetModuleHandleA(0);IMAGE_DOS_HEADER *dos;IMAGE_NT_HEADERS32 *nt;IMAGE_IMPORT_DESCRIPTOR *desc;HMODULE crt;
    if(!base)return;dos=(IMAGE_DOS_HEADER*)base;if(dos->e_magic!=IMAGE_DOS_SIGNATURE)return;
    nt=(IMAGE_NT_HEADERS32*)(base+dos->e_lfanew);if(nt->Signature!=IMAGE_NT_SIGNATURE)return;
    if(!nt->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_IMPORT].VirtualAddress)return;
    crt=GetModuleHandleA("MSVCRT.dll");if(crt){g_real_fopen=(PF_FOPEN)GetProcAddress(crt,"fopen");g_real_fwrite=(PF_FWRITE)GetProcAddress(crt,"fwrite");g_real_fclose=(PF_FCLOSE)GetProcAddress(crt,"fclose");}
    desc=(IMAGE_IMPORT_DESCRIPTOR*)(base+nt->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_IMPORT].VirtualAddress);
    for(;desc->Name;++desc){
        IMAGE_THUNK_DATA32 *names,*slots;if(!desc->OriginalFirstThunk)continue;
        names=(IMAGE_THUNK_DATA32*)(base+desc->OriginalFirstThunk);slots=(IMAGE_THUNK_DATA32*)(base+desc->FirstThunk);
        for(;names->u1.AddressOfData;++names,++slots){FARPROC target;DWORD old;if(IMAGE_SNAP_BY_ORDINAL32(names->u1.Ordinal))continue;
            target=trace_target((const char*)((IMAGE_IMPORT_BY_NAME*)(base+names->u1.AddressOfData))->Name);if(!target)continue;
            if(VirtualProtect(&slots->u1.Function,sizeof(slots->u1.Function),PAGE_READWRITE,&old)){slots->u1.Function=(DWORD)(ULONG_PTR)target;VirtualProtect(&slots->u1.Function,sizeof(slots->u1.Function),old,&old);}
        }
    }
}

static BOOL vendor_control(UCHAR request,USHORT value,BYTE *data,USHORT length){
    WINUSB_SETUP_PACKET packet;ULONG transferred=0;
    ZeroMemory(&packet,sizeof(packet));packet.RequestType=0x41;packet.Request=request;packet.Value=value;packet.Index=0;packet.Length=length;
    return WinUsb_ControlTransfer(g_usb,packet,data,length,&transferred,0)&&transferred==length;
}
static BOOL send_config(BYTE speed,BYTE bits,BYTE parity,BYTE stop){BYTE config[5]={5,speed,bits,parity,stop};return vendor_control(1,0,config,5);}
static BOOL set_read_channel(BOOL enabled){return vendor_control(3,enabled?3:2,0,0);}

static HANDLE open_eos_device_with_guid(const GUID *guid){
    HDEVINFO set;SP_DEVICE_INTERFACE_DATA iface;PSP_DEVICE_INTERFACE_DETAIL_DATA_A detail;DWORD needed,index;HANDLE handle=INVALID_HANDLE_VALUE;
    set=SetupDiGetClassDevsA(guid,0,0,DIGCF_PRESENT|DIGCF_DEVICEINTERFACE);
    if(set==INVALID_HANDLE_VALUE)return INVALID_HANDLE_VALUE;
    ZeroMemory(&iface,sizeof(iface));iface.cbSize=sizeof(iface);
    for(index=0;SetupDiEnumDeviceInterfaces(set,0,guid,index,&iface);++index){
        needed=0;SetupDiGetDeviceInterfaceDetailA(set,&iface,0,0,&needed,0);
        if(!needed)continue;
        detail=(PSP_DEVICE_INTERFACE_DETAIL_DATA_A)HeapAlloc(GetProcessHeap(),HEAP_ZERO_MEMORY,needed);
        if(!detail)break;
        detail->cbSize=sizeof(*detail);
        if(SetupDiGetDeviceInterfaceDetailA(set,&iface,detail,needed,0,0)&&contains_i(detail->DevicePath,EOS_VIDPID)){
            handle=CreateFileA(detail->DevicePath,GENERIC_READ|GENERIC_WRITE,FILE_SHARE_READ|FILE_SHARE_WRITE,0,OPEN_EXISTING,FILE_ATTRIBUTE_NORMAL|FILE_FLAG_OVERLAPPED,0);
            HeapFree(GetProcessHeap(),0,detail);break;
        }
        HeapFree(GetProcessHeap(),0,detail);
    }
    SetupDiDestroyDeviceInfoList(set);return handle;
}
static HANDLE open_eos_device(void){
    HANDLE handle=open_eos_device_with_guid(&eos_usb_interface);
    if(handle==INVALID_HANDLE_VALUE)handle=open_eos_device_with_guid(&generic_usb_interface);
    return handle;
}

static void rx_clear(void){g_rx_head=0;g_rx_count=0;}
static BOOL rx_push(const BYTE *data,DWORD size){
    DWORD i;if(size>RX_CAPACITY-g_rx_count)return FALSE;
    for(i=0;i<size;++i)g_rx[(g_rx_head+g_rx_count+i)%RX_CAPACITY]=data[i];g_rx_count+=size;return TRUE;
}
static DWORD rx_pop(BYTE *data,DWORD size){
    DWORD i,take=size<g_rx_count?size:g_rx_count;
    for(i=0;i<take;++i)data[i]=g_rx[(g_rx_head+i)%RX_CAPACITY];g_rx_head=(g_rx_head+take)%RX_CAPACITY;g_rx_count-=take;return take;
}

static BOOL usb_read_block(DWORD timeout,DWORD *error){
    BYTE block[EOS_BLOCK_SIZE];DWORD policy=timeout,start=GetTickCount();ULONG got=0,payload;
    WinUsb_SetPipePolicy(g_usb,EOS_PIPE_IN,PIPE_TRANSFER_TIMEOUT,sizeof(policy),&policy);
    do{
        got=0;if(!WinUsb_ReadPipe(g_usb,EOS_PIPE_IN,block,sizeof(block),&got,0)){*error=GetLastError();return FALSE;}
        if(!got)Sleep(1);
    }while(!got&&GetTickCount()-start<timeout);
    if(!got){*error=ERROR_SEM_TIMEOUT;return FALSE;}
    if(got<2){*error=ERROR_INVALID_DATA;return FALSE;}
    payload=(DWORD)block[0]|((DWORD)block[1]<<8);
    if(payload>EOS_MAX_PAYLOAD||payload>got-2){*error=ERROR_INVALID_DATA;return FALSE;}
    if(payload&&!rx_push(block+2,payload)){*error=ERROR_BUFFER_OVERFLOW;return FALSE;}
    *error=ERROR_SUCCESS;return TRUE;
}
static BOOL usb_write_bytes(const BYTE *data,DWORD size,DWORD timeout,DWORD *written,DWORD *error){
    BYTE block[EOS_BLOCK_SIZE];DWORD offset=0,chunk,policy=timeout?timeout:1000;ULONG sent;
    WinUsb_SetPipePolicy(g_usb,EOS_PIPE_OUT,PIPE_TRANSFER_TIMEOUT,sizeof(policy),&policy);
    while(offset<size){
        chunk=size-offset;if(chunk>EOS_MAX_PAYLOAD)chunk=EOS_MAX_PAYLOAD;
        ZeroMemory(block,sizeof(block));block[0]=(BYTE)chunk;block[1]=(BYTE)(chunk>>8);CopyMemory(block+2,data+offset,chunk);sent=0;
        if(!WinUsb_WritePipe(g_usb,EOS_PIPE_OUT,block,sizeof(block),&sent,0)||sent!=sizeof(block)){*error=GetLastError();*written=offset;return FALSE;}
        offset+=chunk;
    }
    *written=size;*error=ERROR_SUCCESS;return TRUE;
}

static BYTE baud_code(DWORD baud){if(baud==19200)return 4;if(baud==9600)return 6;return 0;}
static BOOL bridge_open(void){
    g_device=open_eos_device();if(g_device==INVALID_HANDLE_VALUE)return FALSE;
    if(!WinUsb_Initialize(g_device,&g_usb)){CloseHandle(g_device);g_device=INVALID_HANDLE_VALUE;return FALSE;}
    g_comm=CreateEventA(0,FALSE,FALSE,0);if(!g_comm){WinUsb_Free(g_usb);g_usb=0;CloseHandle(g_device);g_device=INVALID_HANDLE_VALUE;return FALSE;}
    rx_clear();WinUsb_AbortPipe(g_usb,EOS_PIPE_IN);WinUsb_AbortPipe(g_usb,EOS_PIPE_OUT);WinUsb_ResetPipe(g_usb,EOS_PIPE_IN);WinUsb_ResetPipe(g_usb,EOS_PIPE_OUT);WinUsb_FlushPipe(g_usb,EOS_PIPE_IN);
    /* Reproduce the observed legacy-driver startup state before SetCommState. */
    if(!send_config(4,7,0,0)||!send_config(6,7,0,0)||!set_read_channel(TRUE))return FALSE;
    return TRUE;
}
static void bridge_close(void){
    if(g_usb){set_read_channel(FALSE);WinUsb_Free(g_usb);g_usb=0;}
    if(g_device!=INVALID_HANDLE_VALUE){CloseHandle(g_device);g_device=INVALID_HANDLE_VALUE;}
    if(g_comm!=INVALID_HANDLE_VALUE){CloseHandle(g_comm);g_comm=INVALID_HANDLE_VALUE;}
    rx_clear();
}
static void send_real_exit_command(void){
    BYTE command=0xf2,reply=0;DWORD written=0,error=ERROR_SUCCESS,reply_error=ERROR_SUCCESS,reply_size=0;BOOL opened=FALSE,ok=FALSE,reply_ok=FALSE,complete=FALSE;
    if(InterlockedExchange(&g_exit_sent,1)!=0)return;
    if(!g_had_session){log_line("EXIT_F2_SKIPPED",0,0,0,0,TRUE,0);return;}
    EnterCriticalSection(&g_state_lock);
    if(g_comm==INVALID_HANDLE_VALUE){
        SetLastError(ERROR_SUCCESS);opened=bridge_open();error=opened?ERROR_SUCCESS:GetLastError();
        log_line("EXIT_REOPEN",g_comm,0,0,0,opened,error);
    }else opened=TRUE;
    if(opened&&g_usb){
        rx_clear();
        ok=usb_write_bytes(&command,1,1000,&written,&error);
        log_line("EXIT_F2_WRITE",g_comm,&command,written,1,ok,error);
        if(ok){
            reply_ok=usb_read_block(1000,&reply_error);
            if(reply_ok)reply_size=rx_pop(&reply,1);
            log_line("EXIT_F2_REPLY",g_comm,reply_size?&reply:0,reply_size,1,reply_ok&&reply_size==1,reply_error);
            if(reply_ok&&reply_size==1&&reply==0xf2)complete=TRUE;
            else if(reply_ok&&reply_size==1&&reply==0xf4){
                command=0xf4;written=0;error=ERROR_SUCCESS;
                ok=usb_write_bytes(&command,1,1000,&written,&error);
                log_line("EXIT_F4_ACK",g_comm,&command,written,1,ok,error);
                if(ok){
                    Sleep(300);rx_clear();command=0xf2;written=0;error=ERROR_SUCCESS;
                    ok=usb_write_bytes(&command,1,1000,&written,&error);
                    log_line("EXIT_FINAL_F2_WRITE",g_comm,&command,written,1,ok,error);
                    if(ok){
                        reply=0;reply_size=0;reply_error=ERROR_SUCCESS;reply_ok=usb_read_block(1000,&reply_error);
                        if(reply_ok)reply_size=rx_pop(&reply,1);
                        complete=reply_ok&&reply_size==1&&reply==0xf2;
                        log_line("EXIT_FINAL_F2_REPLY",g_comm,reply_size?&reply:0,reply_size,1,complete,reply_error);
                    }
                }
            }
        }
    }
    log_line("EXIT_PROTOCOL_COMPLETE",g_comm,0,0,0,complete,complete?ERROR_SUCCESS:ERROR_INVALID_DATA);
    bridge_close();LeaveCriticalSection(&g_state_lock);
}
static VOID WINAPI Trace_PostQuitMessage(int exit_code){
    send_real_exit_command();PostQuitMessage(exit_code);
}

BOOL WINAPI HookSetCommState(HANDLE h,LPDCB d){
    BOOL ok;BYTE code;DWORD error=ERROR_SUCCESS;
    if(h!=g_comm)return SetCommState(h,d);if(!d){SetLastError(ERROR_INVALID_PARAMETER);return FALSE;}
    EnterCriticalSection(&g_state_lock);code=baud_code(d->BaudRate);
    if(!code){ok=FALSE;error=ERROR_NOT_SUPPORTED;}else{ok=send_config(code,d->ByteSize,d->Parity,d->StopBits);if(ok)g_dcb=*d;else error=GetLastError();}
    LeaveCriticalSection(&g_state_lock);log_line("SETCOMM",h,(const BYTE*)d,sizeof(*d),sizeof(*d),ok,error);if(!ok)SetLastError(error);return ok;
}
BOOL WINAPI HookGetCommState(HANDLE h,LPDCB d){
    if(h!=g_comm)return GetCommState(h,d);if(!d){SetLastError(ERROR_INVALID_PARAMETER);return FALSE;}
    EnterCriticalSection(&g_state_lock);*d=g_dcb;LeaveCriticalSection(&g_state_lock);log_line("GETCOMM",h,(const BYTE*)d,sizeof(*d),sizeof(*d),TRUE,0);return TRUE;
}
HANDLE WINAPI HookCreateFileA(LPCSTR name,DWORD access,DWORD share,LPSECURITY_ATTRIBUTES sa,DWORD creation,DWORD flags,HANDLE tmpl){
    char fixed[MAX_PATH];LPCSTR effective;HANDLE h;DWORD error;
    if(!is_com_path(name)){effective=fix_application_path(name,fixed);SetLastError(ERROR_SUCCESS);h=CreateFileA(effective,access,share,sa,creation,flags,tmpl);error=GetLastError();log_memory_redirect(name,effective);if(g_is_memory_process)log_line("MEMORY_CREATEFILE",h,(const BYTE*)name,name?(DWORD)lstrlenA(name):0,access,h!=INVALID_HANDLE_VALUE,error);return h;}
    EnterCriticalSection(&g_state_lock);
    if(g_comm!=INVALID_HANDLE_VALUE){LeaveCriticalSection(&g_state_lock);SetLastError(ERROR_SHARING_VIOLATION);return INVALID_HANDLE_VALUE;}
    if(!bridge_open()){error=GetLastError();bridge_close();LeaveCriticalSection(&g_state_lock);log_line("OPEN",INVALID_HANDLE_VALUE,(const BYTE*)name,name?(DWORD)lstrlenA(name):0,0,FALSE,error);SetLastError(error);return INVALID_HANDLE_VALUE;}
    h=g_comm;g_had_session=TRUE;LeaveCriticalSection(&g_state_lock);log_line("OPEN",h,(const BYTE*)name,name?(DWORD)lstrlenA(name):0,0,TRUE,0);return h;
}
LPSTR WINAPI HooklstrcatA(LPSTR dst,LPCSTR src){return lstrcatA(dst,src);}
HANDLE WINAPI HookCreateSemaphoreA(LPSECURITY_ATTRIBUTES sa,LONG initial,LONG maximum,LPCSTR name){return CreateSemaphoreA(sa,initial,maximum,name);}
BOOL WINAPI HookCloseHandle(HANDLE h){
    if(h!=g_comm)return CloseHandle(h);log_line("CLOSE",h,0,0,0,TRUE,0);EnterCriticalSection(&g_state_lock);bridge_close();LeaveCriticalSection(&g_state_lock);return TRUE;
}
DWORD WINAPI HookWaitForSingleObject(HANDLE h,DWORD ms){return WaitForSingleObject(h,ms);}
BOOL WINAPI HookReleaseSemaphore(HANDLE h,LONG release,LPLONG previous){return ReleaseSemaphore(h,release,previous);}
BOOL WINAPI HookWriteFile(HANDLE h,LPCVOID buffer,DWORD requested,LPDWORD actual,LPOVERLAPPED ov){
    BOOL ok;DWORD done=0,error=0;
    if(h!=g_comm)return WriteFile(h,buffer,requested,actual,ov);if(ov){SetLastError(ERROR_NOT_SUPPORTED);return FALSE;}
    if(requested==1&&((const BYTE*)buffer)[0]==0xf2){
        BYTE response=0xf2;EnterCriticalSection(&g_state_lock);ok=rx_push(&response,1);LeaveCriticalSection(&g_state_lock);
        if(actual)*actual=ok?1:0;error=ok?ERROR_SUCCESS:ERROR_BUFFER_OVERFLOW;log_line("WRITE_SUPPRESSED_F2",h,(const BYTE*)buffer,ok?1:0,requested,ok,error);if(!ok)SetLastError(error);return ok;
    }
    EnterCriticalSection(&g_state_lock);ok=usb_write_bytes((const BYTE*)buffer,requested,g_timeouts.WriteTotalTimeoutConstant,&done,&error);LeaveCriticalSection(&g_state_lock);
    if(actual)*actual=done;log_line("WRITE",h,(const BYTE*)buffer,done,requested,ok,error);if(!ok)SetLastError(error);return ok;
}
BOOL WINAPI HookReadFile(HANDLE h,LPVOID buffer,DWORD requested,LPDWORD actual,LPOVERLAPPED ov){
    BYTE *out=(BYTE*)buffer;DWORD done=0,error=0,timeout;BOOL ok=TRUE;
    if(h!=g_comm)return ReadFile(h,buffer,requested,actual,ov);if(ov){SetLastError(ERROR_NOT_SUPPORTED);return FALSE;}
    EnterCriticalSection(&g_state_lock);timeout=g_timeouts.ReadTotalTimeoutConstant;if(!timeout)timeout=100;
    while(done<requested){
        done+=rx_pop(out+done,requested-done);if(done>=requested)break;
        if(done==1&&(out[0]==0xf4||out[0]==0x00))break;
        if(!usb_read_block(timeout,&error)){
            if(error==ERROR_SEM_TIMEOUT||error==ERROR_OPERATION_ABORTED){error=0;break;}
            ok=FALSE;break;
        }
    }
    LeaveCriticalSection(&g_state_lock);if(actual)*actual=done;log_line("READ",h,out,done,requested,ok,error);if(!ok)SetLastError(error);return ok;
}
BOOL WINAPI HookDisableThreadLibraryCalls(HMODULE m){return DisableThreadLibraryCalls(m);}
BOOL WINAPI HookSetCommTimeouts(HANDLE h,LPCOMMTIMEOUTS t){
    if(h!=g_comm)return SetCommTimeouts(h,t);if(!t){SetLastError(ERROR_INVALID_PARAMETER);return FALSE;}
    EnterCriticalSection(&g_state_lock);g_timeouts=*t;LeaveCriticalSection(&g_state_lock);log_line("TIMEOUTS",h,(const BYTE*)t,sizeof(*t),sizeof(*t),TRUE,0);return TRUE;
}
VOID WINAPI HookSleep(DWORD ms){Sleep(ms);}

LSTATUS WINAPI HookRegOpenKeyExA(HKEY key,LPCSTR subkey,DWORD options,REGSAM access,PHKEY result){
    (void)options;(void)access;
    if(key==HKEY_LOCAL_MACHINE&&equals_i(subkey,"HARDWARE\\DEVICEMAP\\SERIALCOMM")){if(!result)return ERROR_INVALID_PARAMETER;*result=g_fake_serial_key;return ERROR_SUCCESS;}
    return RegOpenKeyExA(key,subkey,options,access,result);
}
LSTATUS WINAPI HookRegCloseKey(HKEY key){if(key==g_fake_serial_key)return ERROR_SUCCESS;return RegCloseKey(key);}
LSTATUS WINAPI HookRegCreateKeyExA(HKEY key,LPCSTR subkey,DWORD reserved,LPSTR class_name,DWORD options,REGSAM access,LPSECURITY_ATTRIBUTES security,PHKEY result,LPDWORD disposition){return RegCreateKeyExA(key,subkey,reserved,class_name,options,access,security,result,disposition);}
LSTATUS WINAPI HookRegQueryInfoKeyA(HKEY key,LPSTR class_name,LPDWORD class_chars,LPDWORD reserved,LPDWORD subkeys,LPDWORD max_subkey_chars,LPDWORD max_class_chars,LPDWORD values,LPDWORD max_value_name_chars,LPDWORD max_value_bytes,LPDWORD security_bytes,PFILETIME last_write){
    (void)class_name;(void)reserved;(void)last_write;
    if(key!=g_fake_serial_key)return RegQueryInfoKeyA(key,class_name,class_chars,reserved,subkeys,max_subkey_chars,max_class_chars,values,max_value_name_chars,max_value_bytes,security_bytes,last_write);
    if(class_chars)*class_chars=0;if(subkeys)*subkeys=0;if(max_subkey_chars)*max_subkey_chars=0;if(max_class_chars)*max_class_chars=0;if(values)*values=1;if(max_value_name_chars)*max_value_name_chars=15;if(max_value_bytes)*max_value_bytes=5;if(security_bytes)*security_bytes=0;return ERROR_SUCCESS;
}
LSTATUS WINAPI HookRegEnumValueA(HKEY key,DWORD index,LPSTR name,LPDWORD name_chars,LPDWORD reserved,LPDWORD type,LPBYTE data,LPDWORD data_bytes){
    static const char value_name[]="\\Device\\EOSmdm0";static const char value_data[]="COM3";DWORD need_name=(DWORD)sizeof(value_name)-1,need_data=(DWORD)sizeof(value_data);
    (void)reserved;
    if(key!=g_fake_serial_key)return RegEnumValueA(key,index,name,name_chars,reserved,type,data,data_bytes);if(index)return ERROR_NO_MORE_ITEMS;if(!name_chars||!data_bytes)return ERROR_INVALID_PARAMETER;
    if(*name_chars<=need_name||*data_bytes<need_data){*name_chars=need_name;*data_bytes=need_data;return ERROR_MORE_DATA;}
    CopyMemory(name,value_name,need_name+1);CopyMemory(data,value_data,need_data);*name_chars=need_name;*data_bytes=need_data;if(type)*type=REG_SZ;return ERROR_SUCCESS;
}
LSTATUS WINAPI HookRegQueryValueExA(HKEY key,LPCSTR value_name,LPDWORD reserved,LPDWORD type,LPBYTE data,LPDWORD data_bytes){
    if(key==g_fake_serial_key)return ERROR_FILE_NOT_FOUND;return RegQueryValueExA(key,value_name,reserved,type,data,data_bytes);
}
LSTATUS WINAPI HookRegSetValueExA(HKEY key,LPCSTR value_name,DWORD reserved,DWORD type,const BYTE *data,DWORD data_bytes){return RegSetValueExA(key,value_name,reserved,type,data,data_bytes);}

BOOL WINAPI DllMain(HINSTANCE module,DWORD reason,LPVOID reserved){
    (void)reserved;
    if(reason==DLL_PROCESS_ATTACH){
        g_module=module;InitializeCriticalSection(&g_state_lock);InitializeCriticalSection(&g_log_lock);DisableThreadLibraryCalls(module);
        g_is_memory_process=main_executable_is("Memory.exe");
        set_module_directory();
        patch_main_file_iat();
        ZeroMemory(&g_timeouts,sizeof(g_timeouts));g_timeouts.ReadIntervalTimeout=200;g_timeouts.ReadTotalTimeoutConstant=200;g_timeouts.WriteTotalTimeoutConstant=1000;
        ZeroMemory(&g_dcb,sizeof(g_dcb));g_dcb.DCBlength=sizeof(g_dcb);g_dcb.BaudRate=9600;g_dcb.fBinary=TRUE;g_dcb.ByteSize=8;g_dcb.Parity=NOPARITY;g_dcb.StopBits=ONESTOPBIT;
    }else if(reason==DLL_PROCESS_DETACH){
        /* Normal users close the pseudo COM handle first. Avoid USB I/O under loader lock. */
        if(g_log!=INVALID_HANDLE_VALUE)CloseHandle(g_log);DeleteCriticalSection(&g_log_lock);DeleteCriticalSection(&g_state_lock);
    }
    return TRUE;
}
