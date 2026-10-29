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
#ifdef __cplusplus
}
#endif
