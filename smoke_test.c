#define WIN32_LEAN_AND_MEAN
#include <windows.h>

void main(void){
    static const char *names[]={"CloseHandle","CreateFileA","CreateSemaphoreA","DisableThreadLibraryCalls","GetCommState","ReadFile","ReleaseSemaphore","SetCommState","SetCommTimeouts","Sleep","WaitForSingleObject","WriteFile","lstrcatA"};
    typedef HANDLE (WINAPI *PF_CREATE)(LPCSTR,DWORD,DWORD,LPSECURITY_ATTRIBUTES,DWORD,DWORD,HANDLE);
    typedef BOOL (WINAPI *PF_WRITE)(HANDLE,LPCVOID,DWORD,LPDWORD,LPOVERLAPPED);
    typedef BOOL (WINAPI *PF_READ)(HANDLE,LPVOID,DWORD,LPDWORD,LPOVERLAPPED);
    typedef BOOL (WINAPI *PF_CLOSE)(HANDLE);
    HMODULE hook=LoadLibraryA("EOSHOOKX.dll");unsigned i;HANDLE file;DWORD done;BYTE out[4]={0xde,0xad,0xbe,0xef},in[4]={0};
    PF_CREATE create;PF_WRITE write;PF_READ read;PF_CLOSE close;
    if(!hook)ExitProcess(10);for(i=0;i<sizeof(names)/sizeof(names[0]);++i)if(!GetProcAddress(hook,names[i]))ExitProcess(20+i);
    create=(PF_CREATE)GetProcAddress(hook,"CreateFileA");write=(PF_WRITE)GetProcAddress(hook,"WriteFile");read=(PF_READ)GetProcAddress(hook,"ReadFile");close=(PF_CLOSE)GetProcAddress(hook,"CloseHandle");
    DeleteFileA("bridge-test.bin");SetLastError(ERROR_SUCCESS);file=create("bridge-test.bin",GENERIC_READ|GENERIC_WRITE,0,0,CREATE_ALWAYS,0,0);if(file==INVALID_HANDLE_VALUE)ExitProcess(GetLastError());
    if(!write(file,out,4,&done,0)||done!=4)ExitProcess(61);SetFilePointer(file,0,0,FILE_BEGIN);if(!read(file,in,4,&done,0)||done!=4)ExitProcess(62);
    if(in[0]!=0xde||in[1]!=0xad||in[2]!=0xbe||in[3]!=0xef)ExitProcess(63);close(file);DeleteFileA("bridge-test.bin");FreeLibrary(hook);ExitProcess(0);
}
