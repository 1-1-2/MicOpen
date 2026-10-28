#include "../src/transport.h"
#include <windows.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
static DWORD WINAPI cancel_after(void *unused){(void)unused;Sleep(1000);transport_cancel();return 0;}
int main(int argc,char **argv)
{
    WSADATA w;SOCKET s;char err[512];HANDLE cancel=NULL;int expect_error;
    if(argc>2&&!strcmp(argv[1],"alive")){
        HANDLE h=OpenProcess(SYNCHRONIZE,FALSE,(DWORD)strtoul(argv[2],NULL,10));
        int alive=h&&WaitForSingleObject(h,0)==WAIT_TIMEOUT;if(h)CloseHandle(h);return alive?0:1;
    }
    if(argc<2)return 2;
    WSAStartup(MAKEWORD(2,2),&w);transport_prepare();
    if(!strcmp(argv[1],"cancel"))cancel=CreateThread(NULL,0,cancel_after,NULL,0,NULL);
    s=transport_connect(3,"auto",8125,err,sizeof(err));
    if(cancel){WaitForSingleObject(cancel,2000);CloseHandle(cancel);}
    expect_error=!strcmp(argv[1],"cancel")||!strcmp(argv[1],"failure");
    if((s==INVALID_SOCKET)!=expect_error){fprintf(stderr,"unexpected connect: %s\n",s==INVALID_SOCKET?err:"success");transport_cleanup();return 4;}
    puts("CONNECTED");fflush(stdout);
    if(!strcmp(argv[1],"hold"))Sleep(INFINITE);
    if(s!=INVALID_SOCKET)closesocket(s);
    if(strcmp(argv[1],"abrupt")){transport_cleanup();transport_cleanup();}
    WSACleanup();return 0;
}
