/* Test fixture only: mimics ADB process lifetimes, never accesses USB devices. */
#include <winsock2.h>
#include <windows.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
static void record(const char *event, int port)
{
    char path[MAX_PATH], line[256]; HANDLE f; DWORD n;
    if (!GetEnvironmentVariableA("MICLINK_TEST_ADB_LOG", path, sizeof(path))) return;
    f=CreateFileA(path,FILE_APPEND_DATA,FILE_SHARE_READ|FILE_SHARE_WRITE,NULL,OPEN_ALWAYS,0,NULL);
    if(f==INVALID_HANDLE_VALUE)return;
    snprintf(line,sizeof(line),"%s %lu %d\n",event,(unsigned long)GetCurrentProcessId(),port);
    WriteFile(f,line,(DWORD)strlen(line),&n,NULL);CloseHandle(f);
}
static void child(void)
{
    char exe[MAX_PATH],cmd[MAX_PATH+32]; STARTUPINFOA si={0}; PROCESS_INFORMATION pi;
    GetModuleFileNameA(NULL,exe,sizeof(exe));snprintf(cmd,sizeof(cmd),"\"%s\" --child",exe);si.cb=sizeof(si);
    if(CreateProcessA(exe,cmd,NULL,NULL,FALSE,CREATE_NO_WINDOW,NULL,NULL,&si,&pi)){CloseHandle(pi.hThread);CloseHandle(pi.hProcess);}
}
int main(int argc,char **argv)
{
    int port=0,i;char value[32];
    if(argc>1&&!strcmp(argv[1],"--child")){record("CHILD",0);Sleep(INFINITE);return 0;}
    if(argc<4||strcmp(argv[1],"-L"))return 2;
    if(sscanf(argv[2],"tcp:127.0.0.1:%d",&port)!=1)return 2;
    if(!strcmp(argv[3],"server")){
        WSADATA w;SOCKET s,p;SOCKADDR_IN a;BOOL exclusive=TRUE;
        WSAStartup(MAKEWORD(2,2),&w);s=socket(AF_INET,SOCK_STREAM,IPPROTO_TCP);
        setsockopt(s,SOL_SOCKET,SO_EXCLUSIVEADDRUSE,(char*)&exclusive,sizeof(exclusive));
        memset(&a,0,sizeof(a));a.sin_family=AF_INET;a.sin_addr.s_addr=htonl(INADDR_LOOPBACK);a.sin_port=htons((u_short)port);
        if(bind(s,(SOCKADDR*)&a,sizeof(a))||listen(s,8))return 3;
        record("SERVER",port);
        if(GetEnvironmentVariableA("MICLINK_TEST_ADB_CHILD",value,sizeof(value)))child();
        while((p=accept(s,NULL,NULL))!=INVALID_SOCKET)closesocket(p);
        return 0;
    }
    record("CLIENT",port);
    for(i=3;i<argc;i++)if(!strcmp(argv[i],"get-serialno")){
        if(GetEnvironmentVariableA("MICLINK_TEST_ADB_HANG",value,sizeof(value))){child();Sleep(60000);}
        puts("TESTUSB");return 0;
    }
    for(i=3;i<argc;i++)if(!strcmp(argv[i],"--remove")){record("REMOVE",port);return 0;}
    if(GetEnvironmentVariableA("MICLINK_TEST_FORWARD_PORT",value,sizeof(value))){puts(value);return 0;}
    return 2;
}
