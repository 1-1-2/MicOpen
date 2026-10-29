#pragma once
#include <windows.h>
#ifdef __cplusplus
extern "C" {
#endif
/* Versioned C ABI. Open/close run on the same transport worker; no audio API.
 * The DLL owns its P2P device handle only, never global Wi-Fi profiles. */
typedef void *(__cdecl *MicDirectOpen)(volatile LONG *, char *, int, char *, int, char *, int);
typedef void (__cdecl *MicDirectClose)(void *);
#ifdef __cplusplus
}
#endif
