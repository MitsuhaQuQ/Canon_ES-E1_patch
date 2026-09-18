#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <bcrypt.h>
#include <strsafe.h>
#include <stdio.h>
#include <wchar.h>
#include <stdarg.h>
#include "bridge_hash.h"
#include "resource.h"

#define HASH_HEX_CHARS 64

static wchar_t g_error[1024];

static const wchar_t REMOTE_ORIGINAL_HASH[] = L"0B3DC2A9CC1EE3690E08B4D0DE52EAACC46F1182922E9B93C94E43CF7BB10C89";
static const wchar_t REMOTE_PATCHED_HASH[]  = L"0A8DAC759136C55D89CA139649C1950D6F7B96F3031B55853A121F1CF900D6B0";
static const wchar_t DRIVER_ORIGINAL_HASH[] = L"B224AE5492BA644A5B5F228DBD7D1E1AF1AAEC7E880FB9DC56D0E0E47FEC66C5";
static const wchar_t DRIVER_PATCHED_HASH[]  = L"F497B4E975462478FFA7366D067F0316085FA5FE312F86A50205F1E84A407CED";
static const wchar_t BRIDGE_HASH[]          = EOS1V_EXPECTED_BRIDGE_HASH;

static void set_error(const wchar_t *format, ...){
    va_list args;
    va_start(args,format);
    StringCchVPrintfW(g_error,ARRAYSIZE(g_error),format,args);
    va_end(args);
}

static void set_win32_error(const wchar_t *operation, const wchar_t *path){
    DWORD error=GetLastError();
    set_error(L"%s failed for\n%s\nWin32 error: %lu",operation,path,(unsigned long)error);
}

static BOOL path_join(wchar_t *output,size_t output_count,const wchar_t *directory,const wchar_t *name){
    size_t length=wcslen(directory);
    const wchar_t *separator=(length&&directory[length-1]!=L'\\'&&directory[length-1]!=L'/')?L"\\":L"";
    return SUCCEEDED(StringCchPrintfW(output,output_count,L"%s%s%s",directory,separator,name));
}

static BOOL file_exists(const wchar_t *path){
    DWORD attributes=GetFileAttributesW(path);
    return attributes!=INVALID_FILE_ATTRIBUTES && !(attributes&FILE_ATTRIBUTE_DIRECTORY);
}

static BOOL sha256_file(const wchar_t *path,wchar_t output[HASH_HEX_CHARS+1]){
    BCRYPT_ALG_HANDLE algorithm=0;
    BCRYPT_HASH_HANDLE hash=0;
    HANDLE file=INVALID_HANDLE_VALUE;
    BYTE *object=0;
    BYTE digest[32];
    BYTE buffer[65536];
    DWORD object_size=0,bytes=0,read=0,i;
    NTSTATUS status;
    BOOL ok=FALSE;

    status=BCryptOpenAlgorithmProvider(&algorithm,BCRYPT_SHA256_ALGORITHM,0,0);
    if(!BCRYPT_SUCCESS(status)){set_error(L"BCryptOpenAlgorithmProvider failed: 0x%08lX",(unsigned long)status);goto cleanup;}
    status=BCryptGetProperty(algorithm,BCRYPT_OBJECT_LENGTH,(PUCHAR)&object_size,sizeof(object_size),&bytes,0);
    if(!BCRYPT_SUCCESS(status)){set_error(L"BCryptGetProperty failed: 0x%08lX",(unsigned long)status);goto cleanup;}
    object=(BYTE*)HeapAlloc(GetProcessHeap(),0,object_size);
    if(!object){set_error(L"Out of memory while hashing.");goto cleanup;}
    status=BCryptCreateHash(algorithm,&hash,object,object_size,0,0,0);
    if(!BCRYPT_SUCCESS(status)){set_error(L"BCryptCreateHash failed: 0x%08lX",(unsigned long)status);goto cleanup;}
    file=CreateFileW(path,GENERIC_READ,FILE_SHARE_READ,0,OPEN_EXISTING,FILE_ATTRIBUTE_NORMAL,0);
    if(file==INVALID_HANDLE_VALUE){set_win32_error(L"Opening file for hashing",path);goto cleanup;}
    for(;;){
        if(!ReadFile(file,buffer,sizeof(buffer),&read,0)){set_win32_error(L"Reading file for hashing",path);goto cleanup;}
        if(!read)break;
        status=BCryptHashData(hash,buffer,read,0);
        if(!BCRYPT_SUCCESS(status)){set_error(L"BCryptHashData failed: 0x%08lX",(unsigned long)status);goto cleanup;}
    }
    status=BCryptFinishHash(hash,digest,sizeof(digest),0);
    if(!BCRYPT_SUCCESS(status)){set_error(L"BCryptFinishHash failed: 0x%08lX",(unsigned long)status);goto cleanup;}
    for(i=0;i<sizeof(digest);++i)StringCchPrintfW(output+i*2,HASH_HEX_CHARS+1-i*2,L"%02X",digest[i]);
    ok=TRUE;
cleanup:
    if(file!=INVALID_HANDLE_VALUE)CloseHandle(file);
    if(hash)BCryptDestroyHash(hash);
    if(object)HeapFree(GetProcessHeap(),0,object);
    if(algorithm)BCryptCloseAlgorithmProvider(algorithm,0);
    return ok;
}

static BOOL read_entire_file(const wchar_t *path,BYTE **data,DWORD *size){
    HANDLE file=INVALID_HANDLE_VALUE;
    LARGE_INTEGER length;
    BYTE *buffer=0;
    DWORD read=0;
    BOOL ok=FALSE;
    file=CreateFileW(path,GENERIC_READ,FILE_SHARE_READ,0,OPEN_EXISTING,FILE_ATTRIBUTE_NORMAL,0);
    if(file==INVALID_HANDLE_VALUE){set_win32_error(L"Opening file",path);goto cleanup;}
    if(!GetFileSizeEx(file,&length)){set_win32_error(L"Reading file size",path);goto cleanup;}
    if(length.QuadPart<=0||length.QuadPart>MAXDWORD){set_error(L"Unsupported file size for\n%s",path);goto cleanup;}
    buffer=(BYTE*)HeapAlloc(GetProcessHeap(),0,(SIZE_T)length.QuadPart);
    if(!buffer){set_error(L"Out of memory while reading\n%s",path);goto cleanup;}
    if(!ReadFile(file,buffer,(DWORD)length.QuadPart,&read,0)||read!=(DWORD)length.QuadPart){set_win32_error(L"Reading file",path);goto cleanup;}
    *data=buffer;*size=read;buffer=0;ok=TRUE;
cleanup:
    if(buffer)HeapFree(GetProcessHeap(),0,buffer);
    if(file!=INVALID_HANDLE_VALUE)CloseHandle(file);
    return ok;
}

static BOOL write_entire_file(const wchar_t *path,const BYTE *data,DWORD size){
    HANDLE file=CreateFileW(path,GENERIC_WRITE,0,0,CREATE_ALWAYS,FILE_ATTRIBUTE_NORMAL,0);
    DWORD written=0;
    BOOL ok=FALSE;
    if(file==INVALID_HANDLE_VALUE){set_win32_error(L"Creating temporary file",path);return FALSE;}
    if(!WriteFile(file,data,size,&written,0)||written!=size){set_win32_error(L"Writing temporary file",path);goto cleanup;}
    if(!FlushFileBuffers(file)){set_win32_error(L"Flushing temporary file",path);goto cleanup;}
    ok=TRUE;
cleanup:
    CloseHandle(file);
    return ok;
}

static BOOL patch_one(const wchar_t *target,const wchar_t *name,const wchar_t *backup_name,const wchar_t *original_hash,const wchar_t *patched_hash,const BYTE *old_bytes,const BYTE *new_bytes,DWORD pattern_size){
    wchar_t path[MAX_PATH],backup[MAX_PATH],temporary[MAX_PATH],hash[HASH_HEX_CHARS+1];
    BYTE *data=0;
    DWORD size=0,i,j,matches=0,position=0,attributes;
    BOOL ok=FALSE;
    temporary[0]=0;
    if(!path_join(path,ARRAYSIZE(path),target,name)||!path_join(backup,ARRAYSIZE(backup),target,backup_name)){set_error(L"Target path is too long.");return FALSE;}
    if(!file_exists(path)){set_error(L"Required Canon file was not found:\n%s",path);return FALSE;}
    if(!sha256_file(path,hash))return FALSE;
    if(_wcsicmp(hash,patched_hash)==0){wprintf(L"%s is already patched.\n",name);return TRUE;}
    if(_wcsicmp(hash,original_hash)!=0){set_error(L"%s has an unsupported SHA-256:\n%s",name,hash);return FALSE;}
    if(file_exists(backup)){
        if(!sha256_file(backup,hash))return FALSE;
        if(_wcsicmp(hash,original_hash)!=0){set_error(L"%s exists but is not the supported original file.",backup_name);return FALSE;}
        wprintf(L"Preserving verified backup %s.\n",backup_name);
    }else{
        if(!CopyFileW(path,backup,TRUE)){set_win32_error(L"Creating original backup",backup);return FALSE;}
        if(!sha256_file(backup,hash)||_wcsicmp(hash,original_hash)!=0){set_error(L"Backup verification failed for %s.",backup_name);return FALSE;}
        wprintf(L"Created verified backup %s.\n",backup_name);
    }
    if(!read_entire_file(path,&data,&size))goto cleanup;
    if(pattern_size>size){set_error(L"Import pattern is larger than %s.",name);goto cleanup;}
    for(i=0;i<=size-pattern_size;++i){
        for(j=0;j<pattern_size&&data[i+j]==old_bytes[j];++j){}
        if(j==pattern_size){position=i;++matches;}
    }
    if(matches!=1){set_error(L"%s contains %lu matching import strings; expected exactly one.",name,(unsigned long)matches);goto cleanup;}
    CopyMemory(data+position,new_bytes,pattern_size);
    if(FAILED(StringCchPrintfW(temporary,ARRAYSIZE(temporary),L"%s.eos1vpatch.tmp",path))){set_error(L"Temporary path is too long.");goto cleanup;}
    DeleteFileW(temporary);
    if(!write_entire_file(temporary,data,size))goto cleanup;
    if(!sha256_file(temporary,hash))goto cleanup;
    if(_wcsicmp(hash,patched_hash)!=0){set_error(L"Patched output verification failed for %s:\n%s",name,hash);goto cleanup;
    }
    attributes=GetFileAttributesW(path);
    if(attributes!=INVALID_FILE_ATTRIBUTES&&(attributes&FILE_ATTRIBUTE_READONLY))SetFileAttributesW(path,attributes&~FILE_ATTRIBUTE_READONLY);
    if(!CopyFileW(temporary,path,FALSE)){set_win32_error(L"Installing patched file",path);goto cleanup;}
    if(attributes!=INVALID_FILE_ATTRIBUTES)SetFileAttributesW(path,attributes);
    if(!sha256_file(path,hash)||_wcsicmp(hash,patched_hash)!=0){set_error(L"Installed file verification failed for %s.",name);goto cleanup;}
    wprintf(L"Patched %s locally.\n",name);ok=TRUE;
cleanup:
    if(data)HeapFree(GetProcessHeap(),0,data);
    if(temporary[0])DeleteFileW(temporary);
    return ok;
}

static BOOL get_executable_directory(wchar_t directory[MAX_PATH]){
    DWORD length=GetModuleFileNameW(0,directory,MAX_PATH);
    wchar_t *slash;
    if(!length||length>=MAX_PATH){set_error(L"Unable to determine the patcher directory.");return FALSE;}
    slash=wcsrchr(directory,L'\\');
    if(!slash){set_error(L"Unable to determine the patcher directory.");return FALSE;}
    *slash=0;return TRUE;
}

static BOOL install_bridge(const wchar_t *target){
    wchar_t destination[MAX_PATH],temporary[MAX_PATH],hash[HASH_HEX_CHARS+1];
    HRSRC resource;
    HGLOBAL loaded;
    const BYTE *data;
    DWORD size,attributes;
    BOOL ok=FALSE;
    temporary[0]=0;
    if(!path_join(destination,ARRAYSIZE(destination),target,L"EOSHOOKX.dll")||FAILED(StringCchPrintfW(temporary,ARRAYSIZE(temporary),L"%s.eos1vpatch.tmp",destination))){set_error(L"Bridge path is too long.");return FALSE;}
    resource=FindResourceW(0,MAKEINTRESOURCEW(IDR_EOSHOOKX),RT_RCDATA);
    if(!resource){set_error(L"The embedded EOSHOOKX.dll resource is missing.");goto cleanup;}
    loaded=LoadResource(0,resource);
    if(!loaded){set_error(L"The embedded EOSHOOKX.dll resource could not be loaded.");goto cleanup;}
    size=SizeofResource(0,resource);
    data=(const BYTE*)LockResource(loaded);
    if(!data||!size){set_error(L"The embedded EOSHOOKX.dll resource is empty.");goto cleanup;}
    DeleteFileW(temporary);
    if(!write_entire_file(temporary,data,size))goto cleanup;
    if(!sha256_file(temporary,hash)||_wcsicmp(hash,BRIDGE_HASH)!=0){set_error(L"The embedded EOSHOOKX.dll failed its package hash check.");goto cleanup;}
    attributes=GetFileAttributesW(destination);
    if(attributes!=INVALID_FILE_ATTRIBUTES&&(attributes&FILE_ATTRIBUTE_READONLY))SetFileAttributesW(destination,attributes&~FILE_ATTRIBUTE_READONLY);
    if(!CopyFileW(temporary,destination,FALSE)){set_win32_error(L"Installing embedded EOSHOOKX.dll",destination);goto cleanup;}
    if(attributes!=INVALID_FILE_ATTRIBUTES)SetFileAttributesW(destination,attributes);
    if(!sha256_file(destination,hash)||_wcsicmp(hash,BRIDGE_HASH)!=0){set_error(L"Installed EOSHOOKX.dll failed verification.");goto cleanup;}
    ok=TRUE;
cleanup:
    if(temporary[0])DeleteFileW(temporary);
    return ok;
}

static void cleanup_legacy_files(const wchar_t *target){
    wchar_t path[MAX_PATH];
    if(path_join(path,ARRAYSIZE(path),target,L"Remote_hooked.exe"))DeleteFileW(path);
    if(path_join(path,ARRAYSIZE(path),target,L"Eos1v_hooked.drv"))DeleteFileW(path);
}

int wmain(int argc,wchar_t **argv){
    static const BYTE remote_old[]="ADVAPI32.dll";
    static const BYTE remote_new[]="EOSHOOKX.dll";
    static const BYTE driver_old[]="KERNEL32.dll";
    static const BYTE driver_new[]="EOSHOOKX.dll";
    wchar_t source_directory[MAX_PATH],target[MAX_PATH],memory[MAX_PATH],message[1400],no_ui[2];
    DWORD attributes;
    BOOL show_ui=GetEnvironmentVariableW(L"EOS1V_PATCHER_NO_UI",no_ui,ARRAYSIZE(no_ui))==0;
    g_error[0]=0;
    if(!get_executable_directory(source_directory))goto failed;
    if(argc>1){
        if(!GetFullPathNameW(argv[1],MAX_PATH,target,0)){set_error(L"The target path is invalid.");goto failed;}
    }else StringCchCopyW(target,ARRAYSIZE(target),source_directory);
    attributes=GetFileAttributesW(target);
    if(attributes==INVALID_FILE_ATTRIBUTES||!(attributes&FILE_ATTRIBUTE_DIRECTORY)){set_error(L"Drop the complete Canon EOS LINK ES-E1 folder onto this EXE, or run the EXE from inside that folder.");goto failed;}
    if(!path_join(memory,ARRAYSIZE(memory),target,L"Memory.exe")||!file_exists(memory)){set_error(L"Memory.exe was not found. Select the complete Canon EOS LINK ES-E1 folder.");goto failed;}
    if(sizeof(remote_old)!=sizeof(remote_new)||sizeof(driver_old)!=sizeof(driver_new)){set_error(L"Internal import patch length mismatch.");goto failed;}
    if(!patch_one(target,L"Remote.exe",L"Remote.original.exe",REMOTE_ORIGINAL_HASH,REMOTE_PATCHED_HASH,remote_old,remote_new,sizeof(remote_old)))goto failed;
    if(!patch_one(target,L"Eos1v.drv",L"Eos1v.original.drv",DRIVER_ORIGINAL_HASH,DRIVER_PATCHED_HASH,driver_old,driver_new,sizeof(driver_old)))goto failed;
    if(!install_bridge(target))goto failed;
    cleanup_legacy_files(target);
    StringCchPrintfW(message,ARRAYSIZE(message),L"Installation completed and verified.\n\nTarget:\n%s\n\nPut the camera in PC mode before starting Remote.exe.",target);
    wprintf(L"%s\n",message);
    if(show_ui)MessageBoxW(0,message,L"EOS-1V Windows 11 compatibility",MB_OK|MB_ICONINFORMATION);
    return 0;
failed:
    if(!g_error[0])StringCchCopyW(g_error,ARRAYSIZE(g_error),L"Unknown patcher error.");
    fwprintf(stderr,L"ERROR: %s\n",g_error);
    if(show_ui)MessageBoxW(0,g_error,L"EOS-1V patcher failed",MB_OK|MB_ICONERROR);
    return 1;
}
