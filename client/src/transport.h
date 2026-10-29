#ifndef MICLINK_TRANSPORT_H
#define MICLINK_TRANSPORT_H
#include <winsock2.h>
/* Modes match the UI: LAN (router or phone hotspot), Wi-Fi Direct, RFCOMM, ADB. */
typedef struct { wchar_t name[248]; char address[13]; } TransportDevice;
int transport_paired_devices(TransportDevice *devices, int capacity);
SOCKET transport_connect(int mode, const char *endpoint, int port, char *error, int cap);
void transport_prepare(void);
void transport_cancel(void);
void transport_cleanup(void);
#endif
