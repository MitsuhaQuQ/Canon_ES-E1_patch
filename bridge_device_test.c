#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdio.h>

typedef HANDLE (WINAPI *PF_CREATE)(LPCSTR,DWORD,DWORD,LPSECURITY_ATTRIBUTES,DWORD,DWORD,HANDLE);
typedef BOOL (WINAPI *PF_CLOSE)(HANDLE);
typedef BOOL (WINAPI *PF_GETCOMM)(HANDLE,LPDCB);
typedef BOOL (WINAPI *PF_SETCOMM)(HANDLE,LPDCB);
typedef BOOL (WINAPI *PF_SETTIMEOUTS)(HANDLE,LPCOMMTIMEOUTS);
typedef BOOL (WINAPI *PF_WRITE)(HANDLE,LPCVOID,DWORD,LPDWORD,LPOVERLAPPED);
typedef BOOL (WINAPI *PF_READ)(HANDLE,LPVOID,DWORD,LPDWORD,LPOVERLAPPED);

static void show(const char *label,const BYTE *data,DWORD size){DWORD i;printf("%-5s",label);for(i=0;i<size;++i)printf(" %02X",data[i]);printf("\n");}
static BOOL checksum(const BYTE *data,DWORD size){DWORD i,sum=0;if(size<3||size!=2+(DWORD)data[1]+1)return FALSE;for(i=2;i<size-1;++i)sum+=data[i];return (BYTE)sum==data[size-1];}

int main(void){
    HMODULE dll=LoadLibraryA("EOSHOOKX.dll");HANDLE h=INVALID_HANDLE_VALUE;PF_CREATE create;PF_CLOSE close;PF_GETCOMM getcomm;PF_SETCOMM setcomm;PF_SETTIMEOUTS settimeouts;PF_WRITE write;PF_READ read;DCB dcb;COMMTIMEOUTS t;BYTE data[64],command;DWORD done,retries=0;int result=1;
    if(!dll){printf("LoadLibrary failed: %lu\n",GetLastError());return 2;}
    create=(PF_CREATE)GetProcAddress(dll,"CreateFileA");close=(PF_CLOSE)GetProcAddress(dll,"CloseHandle");getcomm=(PF_GETCOMM)GetProcAddress(dll,"GetCommState");setcomm=(PF_SETCOMM)GetProcAddress(dll,"SetCommState");settimeouts=(PF_SETTIMEOUTS)GetProcAddress(dll,"SetCommTimeouts");write=(PF_WRITE)GetProcAddress(dll,"WriteFile");read=(PF_READ)GetProcAddress(dll,"ReadFile");
    if(!create||!close||!getcomm||!setcomm||!settimeouts||!write||!read){printf("Missing bridge export.\n");goto done;}
    h=create("\\\\.\\COM3",GENERIC_READ|GENERIC_WRITE,0,0,OPEN_EXISTING,0,0);if(h==INVALID_HANDLE_VALUE){printf("Bridge open failed: %lu\n",GetLastError());goto done;}
    ZeroMemory(&dcb,sizeof(dcb));dcb.DCBlength=sizeof(dcb);if(!getcomm(h,&dcb)){printf("GetCommState failed.\n");goto done;}dcb.BaudRate=9600;dcb.fBinary=TRUE;dcb.ByteSize=8;dcb.Parity=NOPARITY;dcb.StopBits=ONESTOPBIT;if(!setcomm(h,&dcb)){printf("SetCommState failed: %lu\n",GetLastError());goto done;}
    ZeroMemory(&t,sizeof(t));t.ReadIntervalTimeout=200;t.ReadTotalTimeoutConstant=1500;t.WriteTotalTimeoutConstant=1000;if(!settimeouts(h,&t))goto done;
    command=0xff;if(!write(h,&command,1,&done,0)||done!=1||!read(h,data,1,&done,0)||done!=1||(data[0]!=0xf4&&data[0]!=0)){printf("FF handshake failed.\n");goto done;}show("IN",data,done);command=0xf4;if(!write(h,&command,1,&done,0))goto done;Sleep(300);
retry_f6:
    if(++retries>5){printf("Too many F4 retries.\n");goto done;}
    command=0xf6;if(!write(h,&command,1,&done,0)||!read(h,data,17,&done,0)){printf("F6 I/O failed: %lu\n",GetLastError());goto done;}show("IN",data,done);if(done==1&&data[0]==0xf4){command=0xf4;if(!write(h,&command,1,&done,0))goto done;goto retry_f6;}if(done!=17||!checksum(data,done)){printf("F6 validation failed.\n");goto done;}
    command=0xf1;if(!write(h,&command,1,&done,0)||!read(h,data,6,&done,0)){printf("F1 I/O failed: %lu\n",GetLastError());goto done;}show("IN",data,done);if(done!=6||!checksum(data,done)){printf("F1 validation failed.\n");goto done;}
    command=0xf2;if(!write(h,&command,1,&done,0)||!read(h,data,1,&done,0)){printf("F2 close failed.\n");goto done;}show("IN",data,done);if(done==1&&data[0]==0xf4){command=0xf4;if(!write(h,&command,1,&done,0))goto done;command=0xf2;if(!write(h,&command,1,&done,0)||!read(h,data,1,&done,0)||done!=1||data[0]!=0xf2)goto done;show("IN",data,done);}
    printf("Bridge device test completed successfully.\n");result=0;
done:
    if(h!=INVALID_HANDLE_VALUE&&close)close(h);if(dll)FreeLibrary(dll);return result;
}
