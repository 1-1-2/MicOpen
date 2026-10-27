/* Independent monitor branch; all device and DSP ownership stays on its worker. */
#ifndef MICLINK_MONITOR_H
#define MICLINK_MONITOR_H
#include <stdint.h>
#include <windows.h>
#ifdef __cplusplus
extern "C" {
#endif
#define MON_DEVICE_ID 512
#define MON_DEVICE_NAME 192
typedef struct {
    wchar_t id[MON_DEVICE_ID], name[MON_DEVICE_NAME];
} MonitorDevice;
typedef struct {
    int wanted, active, volume, suppression, module, delay_ms;
    int ring_available, aec_available, filters, switching;
    unsigned dropped_frames;
    wchar_t device[MON_DEVICE_NAME], message[256];
} MonitorStatus;
int monitor_init(void);
void monitor_shutdown(void);
int monitor_enable(int on);
void monitor_feed(const int16_t *pcm, int samples);
void monitor_flush(void);
void monitor_device(const wchar_t *id); /* empty: follow default eConsole endpoint */
int monitor_devices(MonitorDevice *out, int cap);
void monitor_volume(int percent);
void monitor_effect(int enabled, int module); /* 0 Ring Out; 1 SpeexDSP AEC */
void monitor_delay(int ms);                   /* playback-to-return delay, 10..400 ms */
void monitor_reset(void);
void monitor_snapshot(MonitorStatus *out);
void monitor_show(HWND owner);
#ifdef __cplusplus
}
#endif
#endif
