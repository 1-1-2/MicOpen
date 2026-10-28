/* Connection adapters only; no audio, protocol or driver dependencies. */
#include "transport.h"
#include <ws2tcpip.h>
#include <ws2bth.h>
#include <windows.h>
#include <bluetoothapis.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

static volatile LONG cancelled;
void transport_cancel(void) { InterlockedExchange(&cancelled, 1); }
void transport_prepare(void) { InterlockedExchange(&cancelled, 0); }
static int bounded_connect(SOCKET sock, SOCKADDR *addr, int len)
{
    u_long nonblock = 1;
    DWORD started = GetTickCount();
    if (sock == INVALID_SOCKET || ioctlsocket(sock, FIONBIO, &nonblock)) return -1;
    if (connect(sock, addr, len)) {
        int err = WSAGetLastError();
        if (err != WSAEWOULDBLOCK && err != WSAEINPROGRESS) return -1;
        for (;;) {
            fd_set wr, ex; struct timeval tv = {0, 100000};
            int result, status = 0, n = sizeof(status);
            if (cancelled || GetTickCount() - started > 6000) { WSASetLastError(WSAETIMEDOUT); return -1; }
            FD_ZERO(&wr); FD_ZERO(&ex); FD_SET(sock,&wr); FD_SET(sock,&ex);
            result = select(0,NULL,&wr,&ex,&tv);
            if (result < 0) return -1;
            if (!result) continue;
            if (getsockopt(sock,SOL_SOCKET,SO_ERROR,(char*)&status,&n) || status) { WSASetLastError(status); return -1; }
            break;
        }
    }
    nonblock = 0;
    return ioctlsocket(sock,FIONBIO,&nonblock);
}
static char usb_serial[128];
static int usb_port;
/* No shell. Only validated serial and numeric ports enter this command line.
 * adb.exe is resolved from PATH by SearchPath, never downloaded or installed. */
static int adb_run(const char *args, char *out, int cap)
{
    char exe[MAX_PATH], command[1024];
    STARTUPINFOA si; PROCESS_INFORMATION pi; SECURITY_ATTRIBUTES sa;
    HANDLE rd, wr; DWORD n = 0, code = 1;
    int cleanup = strstr(args, "--remove") != NULL;
    if (!SearchPathA(NULL, "adb.exe", NULL, MAX_PATH, exe, NULL)) return 0;
    memset(&sa, 0, sizeof(sa)); sa.nLength = sizeof(sa); sa.bInheritHandle = TRUE;
    if (!CreatePipe(&rd, &wr, &sa, 0)) return 0;
    SetHandleInformation(rd, HANDLE_FLAG_INHERIT, 0);
    memset(&si, 0, sizeof(si)); si.cb = sizeof(si);
    si.dwFlags = STARTF_USESTDHANDLES; si.hStdOutput = wr; si.hStdError = wr;
    si.hStdInput = GetStdHandle(STD_INPUT_HANDLE);
    snprintf(command, sizeof(command), "\"%s\" %s", exe, args);
    if (!CreateProcessA(exe, command, NULL, NULL, TRUE, CREATE_NO_WINDOW, NULL, NULL, &si, &pi)) {
        CloseHandle(rd); CloseHandle(wr); return 0;
    }
    CloseHandle(wr);
    {
        DWORD start = GetTickCount();
        while (WaitForSingleObject(pi.hProcess, 50) == WAIT_TIMEOUT) {
            if ((!cleanup && cancelled) || GetTickCount() - start > (cleanup ? 1000u : 8000u)) {
                TerminateProcess(pi.hProcess, 1); WaitForSingleObject(pi.hProcess, 1000); break;
            }
        }
    }
    GetExitCodeProcess(pi.hProcess, &code);
    /* Peek avoids waiting for adb server descendants retaining the pipe. */
    { DWORD available = 0;
      if (PeekNamedPipe(rd, NULL, 0, NULL, &available, NULL) && available)
          ReadFile(rd, out, available < (DWORD)(cap-1) ? available : (DWORD)(cap-1), &n, NULL);
    }
    out[n] = 0;
    CloseHandle(rd); CloseHandle(pi.hThread); CloseHandle(pi.hProcess);
    return code == 0;
}
void transport_cleanup(void)
{
    if (usb_port) {
        char args[256], ignored[512];
        snprintf(args, sizeof(args), "-s %s forward --remove tcp:%d", usb_serial, usb_port);
        adb_run(args, ignored, sizeof(ignored));
        usb_port = 0;
    }
}
SOCKET transport_connect(int mode, const char *endpoint, int port, char *error, int cap)
{
    SOCKET sock = INVALID_SOCKET;
    if (mode == 2) {
        SOCKADDR_BTH addr;
        unsigned long long value = 0; int i;
        /* Exactly 12 hex digits: no channel numbers, names or IP ambiguity. */
        if (strlen(endpoint) != 12) { snprintf(error, cap, "蓝牙地址需为12位十六进制（不含冒号）"); return sock; }
        for (i=0; i<12; i++) {
            char c = endpoint[i]; int d = c>='0'&&c<='9'?c-'0':c>='a'&&c<='f'?c-'a'+10:c>='A'&&c<='F'?c-'A'+10:-1;
            if(d<0) { snprintf(error, cap, "蓝牙地址非法"); return sock; }
            value = (value<<4) | d;
        }
        memset(&addr, 0, sizeof(addr)); addr.addressFamily = AF_BTH; addr.btAddr = value;
        addr.serviceClassId = (GUID){0xad5c9d21,0x8a64,0x487b,{0x9c,0x74,0x6f,0x3a,0xa0,0x69,0x47,0x2e}};
        sock = socket(AF_BTH, SOCK_STREAM, BTHPROTO_RFCOMM);
        if (sock != INVALID_SOCKET && bounded_connect(sock, (SOCKADDR*)&addr, sizeof(addr)) == 0) return sock;
    } else {
        SOCKADDR_IN addr;
        if (mode == 3) {
            char args[256], result[1024], serial[128]; int i; char *end;
            if (!adb_run("start-server",result,sizeof(result))) goto adb_error;
            /* Resolve exactly one USB device to a serial, then use -s for setup AND cleanup. */
            if (!strcmp(endpoint,"auto") || !endpoint[0]) {
                if (!adb_run("-d get-serialno", serial, sizeof(serial))) goto adb_error;
                serial[strcspn(serial,"\r\n")] = 0;
            } else { snprintf(serial, sizeof(serial), "%s", endpoint); }
            if (!serial[0] || strlen(serial)>100 || !strcmp(serial,"unknown")) goto adb_error;
            for(i=0; serial[i]; i++) if (!((serial[i]>='a'&&serial[i]<='z') || (serial[i]>='A'&&serial[i]<='Z') || (serial[i]>='0'&&serial[i]<='9') || serial[i]=='-' || serial[i]=='_' || serial[i]=='.')) goto adb_error;
            snprintf(args,sizeof(args),"-s %s forward tcp:0 tcp:%d",serial,port);
            if (!adb_run(args,result,sizeof(result))) goto adb_error;
            port = (int)strtol(result,&end,10);
            if(port<1 || port>65535 || (*end && *end!='\r' && *end!='\n')) goto adb_error;
            strcpy(usb_serial, serial); usb_port = port; endpoint = "127.0.0.1";
        }
        memset(&addr,0,sizeof(addr)); addr.sin_family=AF_INET; addr.sin_port=htons((u_short)port);
        if(InetPtonA(AF_INET,endpoint,&addr.sin_addr)!=1) {
            struct hostent *he=gethostbyname(endpoint);
            if(!he) { snprintf(error,cap,"地址解析失败"); return INVALID_SOCKET; }
            memcpy(&addr.sin_addr,he->h_addr_list[0],4);
        }
        sock=socket(AF_INET,SOCK_STREAM,IPPROTO_TCP);
        if(sock!=INVALID_SOCKET && bounded_connect(sock,(SOCKADDR*)&addr,sizeof(addr))==0) {
            BOOL yes=TRUE; setsockopt(sock,IPPROTO_TCP,TCP_NODELAY,(char*)&yes,sizeof(yes)); return sock;
        }
    }
    snprintf(error,cap,"连接失败 WSA=%d",WSAGetLastError());
    if(sock!=INVALID_SOCKET) closesocket(sock);
    return INVALID_SOCKET;
adb_error:
    snprintf(error,cap,"ADB未就绪：检查PATH中的adb.exe、USB调试授权及设备序列号；auto要求仅一台USB设备");
    return INVALID_SOCKET;
}

int transport_paired_devices(TransportDevice *devices, int capacity)
{
    BLUETOOTH_DEVICE_SEARCH_PARAMS params;
    BLUETOOTH_DEVICE_INFO info;
    HBLUETOOTH_DEVICE_FIND find;
    int count = 0;
    memset(&params,0,sizeof(params)); params.dwSize=sizeof(params);
    params.fReturnAuthenticated=TRUE; params.fReturnRemembered=TRUE;
    memset(&info,0,sizeof(info)); info.dwSize=sizeof(info);
    find=BluetoothFindFirstDevice(&params,&info);
    if (!find) return 0;
    do {
        if(count >= capacity) break;
        wcsncpy(devices[count].name,info.szName,247); devices[count].name[247]=0;
        snprintf(devices[count].address,13,"%012llX",(unsigned long long)info.Address.ullLong);
        count++;
    } while(BluetoothFindNextDevice(find,&info));
    BluetoothFindDeviceClose(find);
    return count;
}
