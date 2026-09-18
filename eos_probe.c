#define WIN32_LEAN_AND_MEAN
#define _WIN32_WINNT 0x0601
#include <windows.h>
#include <setupapi.h>
#include <winusb.h>
#include <stdio.h>

#define VIDPID "vid_04a9&pid_3040"
#define PIPE_IN 0x81
#define PIPE_OUT 0x02

static const GUID project_guid={0x290DECB5,0x2458,0x41EC,{0x87,0xA8,0x4D,0x09,0xBD,0xD2,0x0C,0xE2}};
static const GUID usb_guid={0xA5DCBF10,0x6530,0x11D2,{0x90,0x1F,0x00,0xC0,0x4F,0xB9,0x51,0xED}};

static int lower_ascii(int c){return c>='A'&&c<='Z'?c+32:c;}
static int contains_i(const char *s,const char *part){const char *a,*b;for(;s&&*s;++s){a=s;b=part;while(*a&&*b&&lower_ascii((unsigned char)*a)==lower_ascii((unsigned char)*b)){++a;++b;}if(!*b)return 1;}return 0;}
static void print_bytes(const char *label,const BYTE *data,DWORD size){DWORD i;printf("%-5s",label);for(i=0;i<size;++i)printf(" %02X",data[i]);printf("\n");}

static HANDLE open_with_guid(const GUID *guid){
    HDEVINFO set=SetupDiGetClassDevsA(guid,0,0,DIGCF_PRESENT|DIGCF_DEVICEINTERFACE);SP_DEVICE_INTERFACE_DATA iface;DWORD index,needed;HANDLE h=INVALID_HANDLE_VALUE;
    if(set==INVALID_HANDLE_VALUE)return h;ZeroMemory(&iface,sizeof(iface));iface.cbSize=sizeof(iface);
    for(index=0;SetupDiEnumDeviceInterfaces(set,0,guid,index,&iface);++index){
        PSP_DEVICE_INTERFACE_DETAIL_DATA_A detail;needed=0;SetupDiGetDeviceInterfaceDetailA(set,&iface,0,0,&needed,0);if(!needed)continue;
        detail=(PSP_DEVICE_INTERFACE_DETAIL_DATA_A)HeapAlloc(GetProcessHeap(),HEAP_ZERO_MEMORY,needed);if(!detail)break;detail->cbSize=sizeof(*detail);
        if(SetupDiGetDeviceInterfaceDetailA(set,&iface,detail,needed,0,0)&&contains_i(detail->DevicePath,VIDPID)){
            printf("Device: %s\n",detail->DevicePath);h=CreateFileA(detail->DevicePath,GENERIC_READ|GENERIC_WRITE,FILE_SHARE_READ|FILE_SHARE_WRITE,0,OPEN_EXISTING,FILE_ATTRIBUTE_NORMAL|FILE_FLAG_OVERLAPPED,0);HeapFree(GetProcessHeap(),0,detail);break;
        }HeapFree(GetProcessHeap(),0,detail);
    }SetupDiDestroyDeviceInfoList(set);return h;
}
static HANDLE open_device(void){HANDLE h=open_with_guid(&project_guid);return h==INVALID_HANDLE_VALUE?open_with_guid(&usb_guid):h;}
static BOOL control(WINUSB_INTERFACE_HANDLE usb,UCHAR request,USHORT value,BYTE *data,USHORT length){WINUSB_SETUP_PACKET p;ULONG done=0;BOOL ok;ZeroMemory(&p,sizeof(p));p.RequestType=0x41;p.Request=request;p.Value=value;p.Length=length;ok=WinUsb_ControlTransfer(usb,p,data,length,&done,0)&&done==length;printf("CTRL  req=%u value=%u length=%u result=%u done=%lu error=%lu\n",request,value,length,ok,done,ok?0:GetLastError());return ok;}
static BOOL config(WINUSB_INTERFACE_HANDLE usb,BYTE speed,BYTE bits){BYTE data[5]={5,speed,bits,0,0};return control(usb,1,0,data,5);}
static BOOL write_payload(WINUSB_INTERFACE_HANDLE usb,const BYTE *data,DWORD size){BYTE block[64];DWORD timeout=1000;ULONG done=0;BOOL ok;if(size>62)return FALSE;ZeroMemory(block,sizeof(block));block[0]=(BYTE)size;block[1]=(BYTE)(size>>8);CopyMemory(block+2,data,size);WinUsb_SetPipePolicy(usb,PIPE_OUT,PIPE_TRANSFER_TIMEOUT,sizeof(timeout),&timeout);print_bytes("OUT",data,size);ok=WinUsb_WritePipe(usb,PIPE_OUT,block,sizeof(block),&done,0)&&done==sizeof(block);printf("      write result=%u done=%lu error=%lu\n",ok,done,ok?0:GetLastError());return ok;}
static BOOL read_payload(WINUSB_INTERFACE_HANDLE usb,BYTE *data,DWORD capacity,DWORD *size,DWORD timeout){BYTE block[64];ULONG got=0;DWORD length,error,start=GetTickCount();WinUsb_SetPipePolicy(usb,PIPE_IN,PIPE_TRANSFER_TIMEOUT,sizeof(timeout),&timeout);do{got=0;if(!WinUsb_ReadPipe(usb,PIPE_IN,block,sizeof(block),&got,0)){error=GetLastError();printf("      read failed: error=%lu\n",error);return FALSE;}if(!got){printf("      zero-length USB read, retrying\n");Sleep(1);}}while(!got&&GetTickCount()-start<timeout);printf("      raw read bytes=%lu\n",got);if(got<2){print_bytes("RAW",block,got);return FALSE;}length=(DWORD)block[0]|((DWORD)block[1]<<8);if(length>62||length>got-2||length>capacity){print_bytes("RAW",block,got);printf("      invalid payload length=%lu\n",length);return FALSE;}CopyMemory(data,block+2,length);*size=length;print_bytes("IN",data,length);return TRUE;}
static void print_pipes(WINUSB_INTERFACE_HANDLE usb){USB_INTERFACE_DESCRIPTOR d;WINUSB_PIPE_INFORMATION p;UCHAR i;if(!WinUsb_QueryInterfaceSettings(usb,0,&d)){printf("QueryInterfaceSettings failed: %lu\n",GetLastError());return;}printf("Interface %u, endpoints %u, class %02X/%02X/%02X\n",d.bInterfaceNumber,d.bNumEndpoints,d.bInterfaceClass,d.bInterfaceSubClass,d.bInterfaceProtocol);for(i=0;i<d.bNumEndpoints;++i){if(WinUsb_QueryPipe(usb,0,i,&p))printf("Pipe %u: type=%u id=0x%02X max=%u interval=%u\n",i,p.PipeType,p.PipeId,p.MaximumPacketSize,p.Interval);}}
static BOOL read_frame(WINUSB_INTERFACE_HANDLE usb,BYTE command,BYTE *frame,DWORD capacity,DWORD *frame_size){DWORD have=0,got,total=0;BYTE f4=0xf4;while(have<capacity){if(!read_payload(usb,frame+have,capacity-have,&got,1500))return FALSE;if(!have&&got==1&&frame[0]==0xf4){printf("Async F4: echo and retry %02X\n",command);if(!write_payload(usb,&f4,1)||!write_payload(usb,&command,1))return FALSE;continue;}have+=got;if(have>=2){if(frame[0]!=command)return FALSE;total=2+(DWORD)frame[1]+1;if(total>capacity)return FALSE;if(have>=total){*frame_size=total;return TRUE;}}}return FALSE;}
static BOOL check_frame(const BYTE *frame,DWORD size){DWORD i,sum=0;if(size<3||size!=2+(DWORD)frame[1]+1)return FALSE;for(i=2;i<size-1;++i)sum+=frame[i];return (BYTE)sum==frame[size-1];}
static void close_session(WINUSB_INTERFACE_HANDLE usb){BYTE command=0xf2,data[8];DWORD size=0;if(!write_payload(usb,&command,1))return;if(!read_payload(usb,data,sizeof(data),&size,500)||size!=1)return;if(data[0]==0xf4){command=0xf4;if(!write_payload(usb,&command,1))return;command=0xf2;if(!write_payload(usb,&command,1))return;read_payload(usb,data,sizeof(data),&size,500);}}

int main(void){
    HANDLE device=INVALID_HANDLE_VALUE;WINUSB_INTERFACE_HANDLE usb=0;BYTE data[256],one;DWORD size=0;int result=1;
    printf("EOS-1V WinUSB read-only probe\n");device=open_device();if(device==INVALID_HANDLE_VALUE){printf("EOS-1V WinUSB interface not found (error %lu).\n",GetLastError());return 2;}
    if(!WinUsb_Initialize(device,&usb)){printf("WinUsb_Initialize failed: %lu\n",GetLastError());goto done;}print_pipes(usb);
    WinUsb_AbortPipe(usb,PIPE_IN);WinUsb_AbortPipe(usb,PIPE_OUT);WinUsb_ResetPipe(usb,PIPE_IN);WinUsb_ResetPipe(usb,PIPE_OUT);WinUsb_FlushPipe(usb,PIPE_IN);
    if(!config(usb,4,7)||!config(usb,6,7)||!control(usb,3,3,0,0)||!config(usb,6,8)){printf("USB initialization failed: %lu\n",GetLastError());goto done;}
    one=0xff;if(!write_payload(usb,&one,1)||!read_payload(usb,data,sizeof(data),&size,1500)||size!=1||(data[0]!=0xf4&&data[0]!=0x00)){printf("FF/F4 handshake failed.\n");goto done;}
    /* Eos1v.drv converts a single 00 into an F4 echo on the wire as well. */
    one=0xf4;if(!write_payload(usb,&one,1)){printf("F4 echo failed.\n");goto done;}Sleep(300);
    one=0xf6;if(!write_payload(usb,&one,1)||!read_frame(usb,0xf6,data,sizeof(data),&size)||!check_frame(data,size)){printf("F6 query failed.\n");goto done;}printf("F6 checksum OK\n");
    one=0xf1;if(!write_payload(usb,&one,1)||!read_frame(usb,0xf1,data,sizeof(data),&size)||!check_frame(data,size)){printf("F1 query failed.\n");goto done;}printf("F1 checksum OK\n");result=0;
done:
    if(usb){close_session(usb);control(usb,3,2,0,0);WinUsb_Free(usb);}if(device!=INVALID_HANDLE_VALUE)CloseHandle(device);
    printf(result?"Probe failed.\n":"Probe completed successfully.\n");return result;
}
