#pragma once
#include <windows.h>
#ifdef __cplusplus
extern "C" {
#endif
#define MIC_DIRECT_ABI 3
typedef struct {
    unsigned background, surface, text, muted, accent;
    int dark;
} MicDirectTheme;
/* Listener and its close run on the same transport STA worker. */
typedef void *(__cdecl *MicDirectListen)(volatile LONG *, HWND, const MicDirectTheme *,
    char *, int, char *, int, char *, int);
typedef void (__cdecl *MicDirectClose)(void *);
/* Optional ABI 3 extension. Callback runs on the transport worker, no PIN data. */
typedef void (__cdecl *MicDirectLog)(const char *, int);
typedef void (__cdecl *MicDirectSetLogger)(MicDirectLog);
#ifdef __cplusplus
}
#endif
