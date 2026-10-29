#pragma once
#include <windows.h>
#ifdef __cplusplus
extern "C" {
#endif
#define MIC_DIRECT_ABI 2
#define MIC_DIRECT_ID_CAP 2048
typedef struct {
    char id[MIC_DIRECT_ID_CAP];
    wchar_t name[248];
} MicDirectDevice;
typedef struct {
    unsigned background, surface, text, muted, accent;
    int dark;
} MicDirectTheme;
typedef int (__cdecl *MicDirectPick)(volatile LONG *, HWND, int, int,
    const MicDirectTheme *, MicDirectDevice *, char *, int);
typedef void *(__cdecl *MicDirectConnect)(volatile LONG *, const char *, HWND,
    const MicDirectTheme *, char *, int, char *, int, char *, int);
/* Versioned C ABI. Open/close run on the same transport worker; no audio API.
 * The DLL owns its P2P device handle only, never global Wi-Fi profiles. */
typedef void *(__cdecl *MicDirectOpen)(volatile LONG *, char *, int, char *, int, char *, int);
typedef void (__cdecl *MicDirectClose)(void *);
#ifdef __cplusplus
}
#endif
