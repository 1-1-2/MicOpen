/* Connection adapters only; no audio, protocol or driver dependencies. */
#include "transport.h"
#include "../direct/direct.h"
#include <ws2tcpip.h>
#include <ws2bth.h>
#include <windows.h>
#include <bluetoothapis.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

static volatile LONG cancelled;
static HMODULE direct_module;
static void *direct_session;
static MicDirectClose direct_close;
static unsigned direct_abi;

static INIT_ONCE direct_once = INIT_ONCE_STATIC_INIT;
static HWND direct_owner;
static MicDirectTheme direct_theme;
/* Load once from the executable directory, including concurrent UI/engine
 * callers. Failure stays local to Direct and never affects other transports. */
static BOOL CALLBACK load_direct(PINIT_ONCE once, PVOID arg, PVOID *context)
{
    wchar_t path[MAX_PATH], *slash;
    DWORD n = GetModuleFileNameW(NULL, path, MAX_PATH);
    (void)once; (void)arg; (void)context;
    if (!n || n >= MAX_PATH) return TRUE;
    slash = wcsrchr(path, L'\\');
    if (!slash || slash - path + 23 >= MAX_PATH) return TRUE;
    wcscpy(slash + 1, L"miclink-direct.dll");
    direct_module = LoadLibraryExW(path, NULL, LOAD_LIBRARY_SEARCH_DLL_LOAD_DIR | LOAD_LIBRARY_SEARCH_SYSTEM32);
    if (direct_module) {
        unsigned (__cdecl *abi)(void)=(unsigned (__cdecl *)(void))GetProcAddress(direct_module,"ml_direct_abi");
        direct_abi=abi?abi():0;
        direct_close=(MicDirectClose)GetProcAddress(direct_module,"ml_direct_close");
    }
    return TRUE;
}
static int direct_available(char *error, int cap)
{
    InitOnceExecuteOnce(&direct_once, load_direct, NULL, NULL);
    if (!direct_module) {
        snprintf(error,cap,"缺少无线直连模块：请将 miclink-direct.dll 与客户端放在同一目录"); return 0;
    }
    if (direct_abi!=MIC_DIRECT_ABI || !direct_close) {
        snprintf(error,cap,"无线直连模块版本不匹配，请更新完整客户端"); return 0;
    }
    return 1;
}
void transport_configure_direct(HWND owner, const MicDirectTheme *theme)
{
    direct_owner=owner;
    if (theme) direct_theme=*theme;
}
static int direct_open(char *remote, char *local, char *error, int cap)
{
    if (!direct_available(error,cap)) return 0;
    MicDirectListen listen = (MicDirectListen)GetProcAddress(direct_module,"ml_direct_listen");
    if (!listen) {snprintf(error,cap,"无线直连模块缺少等待接口");return 0;}
    direct_session = listen(&cancelled,direct_owner,&direct_theme,remote,16,local,16,error,cap);
    return direct_session != NULL;
}
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
#ifndef MICLINK_ADB_SHARED_PORT
#define MICLINK_ADB_SHARED_PORT 5037
#endif
static char usb_serial[128];
static int usb_port, adb_server_port;
static char adb_exe[MAX_PATH];
static HANDLE adb_job, adb_server;

/* Own only the processes launched by this adapter, never an existing ADB service.
 * Suspended creation closes the gap between process launch and job assignment. */
static int adb_spawn(char *command, STARTUPINFOA *si, PROCESS_INFORMATION *pi)
{
    if (!CreateProcessA(adb_exe, command, NULL, NULL, TRUE,
                        CREATE_NO_WINDOW | CREATE_SUSPENDED, NULL, NULL, si, pi)) return 0;
    if (!AssignProcessToJobObject(adb_job, pi->hProcess) || ResumeThread(pi->hThread) == (DWORD)-1) {
        TerminateProcess(pi->hProcess, 1);
        WaitForSingleObject(pi->hProcess, 1000);
        CloseHandle(pi->hThread); CloseHandle(pi->hProcess);
        return 0;
    }
    return 1;
}
static int adb_listening(int port)
{
    SOCKET sock; SOCKADDR_IN addr; u_long nonblock = 1;
    fd_set wr; struct timeval tv = {0, 100000};
    int err = 1, len = sizeof(err), result;
    sock = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (sock == INVALID_SOCKET) return 0;
    memset(&addr, 0, sizeof(addr)); addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK); addr.sin_port = htons((u_short)port);
    ioctlsocket(sock, FIONBIO, &nonblock);
    result = connect(sock, (SOCKADDR*)&addr, sizeof(addr));
    if (result) {
        FD_ZERO(&wr); FD_SET(sock, &wr);
        if (WSAGetLastError() == WSAEWOULDBLOCK && select(0, NULL, &wr, NULL, &tv) > 0)
            getsockopt(sock, SOL_SOCKET, SO_ERROR, (char*)&err, &len);
    } else err = 0;
    closesocket(sock);
    return err == 0;
}
static int adb_prepare(void)
{
    JOBOBJECT_EXTENDED_LIMIT_INFORMATION limit;
    STARTUPINFOA si; PROCESS_INFORMATION pi; SECURITY_ATTRIBUTES sa;
    SOCKET reserve; SOCKADDR_IN addr; int len = sizeof(addr); BOOL exclusive = TRUE;
    char command[1024]; HANDLE nullio; DWORD started, found;
    if (cancelled) return 0;
    found = SearchPathA(NULL, "adb.exe", NULL, MAX_PATH, adb_exe, NULL);
    if (!found || found >= MAX_PATH) return 0;
    adb_job = CreateJobObjectA(NULL, NULL);
    if (!adb_job) return 0;
    memset(&limit, 0, sizeof(limit));
    limit.BasicLimitInformation.LimitFlags = JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE;
    if (!SetInformationJobObject(adb_job, JobObjectExtendedLimitInformation, &limit, sizeof(limit))) return 0;
    /* A pre-existing default server is outside our job and remains untouched. */
    if (adb_listening(MICLINK_ADB_SHARED_PORT)) {
        adb_server_port = MICLINK_ADB_SHARED_PORT;
        return 1;
    }
    reserve = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (reserve == INVALID_SOCKET) return 0;
    memset(&addr, 0, sizeof(addr)); addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    setsockopt(reserve, SOL_SOCKET, SO_EXCLUSIVEADDRUSE, (char*)&exclusive, sizeof(exclusive));
    if (bind(reserve, (SOCKADDR*)&addr, sizeof(addr)) ||
        getsockname(reserve, (SOCKADDR*)&addr, &len)) { closesocket(reserve); return 0; }
    adb_server_port = ntohs(addr.sin_port);
    closesocket(reserve);
    memset(&sa, 0, sizeof(sa)); sa.nLength = sizeof(sa); sa.bInheritHandle = TRUE;
    nullio = CreateFileA("NUL", GENERIC_READ | GENERIC_WRITE,
                        FILE_SHARE_READ | FILE_SHARE_WRITE, &sa, OPEN_EXISTING, 0, NULL);
    if (nullio == INVALID_HANDLE_VALUE) return 0;
    memset(&si, 0, sizeof(si)); si.cb = sizeof(si); si.dwFlags = STARTF_USESTDHANDLES;
    si.hStdInput = si.hStdOutput = si.hStdError = nullio;
    snprintf(command, sizeof(command), "\"%s\" -L tcp:127.0.0.1:%d server nodaemon", adb_exe, adb_server_port);
    if (!adb_spawn(command, &si, &pi)) { CloseHandle(nullio); return 0; }
    CloseHandle(nullio); CloseHandle(pi.hThread); adb_server = pi.hProcess;
    started = GetTickCount();
    while (!cancelled && GetTickCount() - started < 6000) {
        if (WaitForSingleObject(adb_server, 0) != WAIT_TIMEOUT) return 0;
        if (adb_listening(adb_server_port)) return 1;
        Sleep(25);
    }
    return 0;
}
/* Explicit socket overrides ambient ADB settings; no shell or global kill-server.
 * Command children join the same job, including any daemon they might spawn. */
static int adb_run(const char *args, char *out, int cap)
{
    char command[1024];
    STARTUPINFOA si; PROCESS_INFORMATION pi; SECURITY_ATTRIBUTES sa;
    HANDLE rd, wr; DWORD n = 0, code = 1;
    int cleanup = strstr(args, "--remove") != NULL;
    if (!adb_job || !adb_server_port || cap < 1) return 0;
    memset(&sa, 0, sizeof(sa)); sa.nLength = sizeof(sa); sa.bInheritHandle = TRUE;
    if (!CreatePipe(&rd, &wr, &sa, 0)) return 0;
    SetHandleInformation(rd, HANDLE_FLAG_INHERIT, 0);
    memset(&si, 0, sizeof(si)); si.cb = sizeof(si);
    si.dwFlags = STARTF_USESTDHANDLES; si.hStdOutput = wr; si.hStdError = wr;
    si.hStdInput = GetStdHandle(STD_INPUT_HANDLE);
    snprintf(command, sizeof(command), "\"%s\" -L tcp:127.0.0.1:%d %s", adb_exe, adb_server_port, args);
    if (!adb_spawn(command, &si, &pi)) { CloseHandle(rd); CloseHandle(wr); return 0; }
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
    if (direct_session && direct_close) { direct_close(direct_session); direct_session = NULL; }
    if (usb_port && adb_listening(adb_server_port)) {
        char args[256], ignored[512];
        snprintf(args, sizeof(args), "-s %s forward --remove tcp:%d", usb_serial, usb_port);
        adb_run(args, ignored, sizeof(ignored));
    }
    usb_port = 0; usb_serial[0] = 0;
    /* Last handle closes on normal cleanup OR parent termination. Existing shared
     * ADB servers were never assigned to this job and cannot be killed here. */
    if (adb_job) { CloseHandle(adb_job); adb_job = NULL; }
    if (adb_server) {
        WaitForSingleObject(adb_server, 1000);
        CloseHandle(adb_server); adb_server = NULL;
    }
    adb_server_port = 0; adb_exe[0] = 0;
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
        char direct_remote[16], direct_local[16] = {0};
        if (mode == 1) {
            if (!direct_open(direct_remote, direct_local, error, cap)) return INVALID_SOCKET;
            endpoint = direct_remote;
        }
        if (mode == 3) {
            char args[256], result[1024], serial[128]; int i; char *end;
            if (!adb_prepare()) goto adb_error;
            /* Resolve exactly one USB device to a serial, then use -s for setup AND cleanup. */
            if (!strcmp(endpoint,"auto") || !endpoint[0]) {
                if (!adb_run("-d wait-for-device", result, sizeof(result)) ||
                    !adb_run("-d get-serialno", serial, sizeof(serial))) goto adb_error;
                serial[strcspn(serial,"\r\n")] = 0;
            } else { snprintf(serial, sizeof(serial), "%s", endpoint); }
            if (!serial[0] || strlen(serial)>100 || !strcmp(serial,"unknown")) goto adb_error;
            for(i=0; serial[i]; i++) if (!((serial[i]>='a'&&serial[i]<='z') || (serial[i]>='A'&&serial[i]<='Z') || (serial[i]>='0'&&serial[i]<='9') || serial[i]=='-' || serial[i]=='_' || serial[i]=='.')) goto adb_error;
            snprintf(args,sizeof(args),"-s %s wait-for-device",serial);
            if (!adb_run(args,result,sizeof(result))) goto adb_error;
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
        {
            DWORD started=GetTickCount();int last_error=0;
            do {
                sock=socket(AF_INET,SOCK_STREAM,IPPROTO_TCP);
                if (sock!=INVALID_SOCKET && mode==1) {
                    SOCKADDR_IN source;
                    memset(&source,0,sizeof(source));source.sin_family=AF_INET;
                    InetPtonA(AF_INET,direct_local,&source.sin_addr);
                    if (bind(sock,(SOCKADDR*)&source,sizeof(source))) {
                        snprintf(error,cap,"无法绑定无线直连接口 WSA=%d",WSAGetLastError());
                        closesocket(sock);return INVALID_SOCKET;
                    }
                }
                if (sock!=INVALID_SOCKET && bounded_connect(sock,(SOCKADDR*)&addr,sizeof(addr))==0) {
                    BOOL yes=TRUE;setsockopt(sock,IPPROTO_TCP,TCP_NODELAY,(char*)&yes,sizeof(yes));return sock;
                }
                last_error=WSAGetLastError();
                if (sock!=INVALID_SOCKET) closesocket(sock);
                sock=INVALID_SOCKET;
                if (mode!=1 || cancelled || last_error!=WSAECONNREFUSED || GetTickCount()-started>=5000) break;
                /* L2 can be ready before Android has bound its control server. */
                Sleep(100);
            } while (!cancelled);
            WSASetLastError(last_error);
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
