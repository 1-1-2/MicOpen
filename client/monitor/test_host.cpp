/* Windows worker lifecycle / missing-endpoint checks, no physical microphone. */
#include "../src/monitor.h"
#include <cstdio>
int main() {
    MonitorStatus s;
    MonitorDevice d[128];
    int failures = 0;
    if (!monitor_init())
        return 1;
    for (int i = 0; i < 100; i++) {
        monitor_snapshot(&s);
        if (s.ring_available && s.aec_available)
            break;
        Sleep(50);
    }
    if (!s.ring_available || !s.aec_available) {
        puts("FAIL module loading on monitor worker");
        failures++;
    }
    int count = monitor_devices(d, 128);
    printf("Active render endpoints: %d\n", count);
    // Invalid explicit endpoint must never fall back to a loudspeaker.
    monitor_device(L"{MicLink-deliberately-absent-test-endpoint}");
    monitor_enable(1);
    Sleep(300);
    int16_t silence[960] = {};
    for (int i = 0; i < 60; i++) {
        monitor_feed(silence, 960);
        Sleep(1);
    }
    monitor_snapshot(&s);
    if (s.active) {
        puts("FAIL missing explicit endpoint silently fell back");
        failures++;
    } else
        puts("PASS missing explicit endpoint stays paused");
    monitor_effect(1, 1);
    monitor_delay(170);
    for (int i = 0; i < 3; ++i) monitor_reset();
    Sleep(50);
    monitor_snapshot(&s);
    if (s.delay_ms != 170) { puts("FAIL reset changed AEC delay"); failures++; }
    else puts("PASS reset preserves AEC delay");
    for (int j = 0; j < 20; j++) {
        monitor_volume(j * 5);
        monitor_effect(j % 2, j % 2);
        monitor_delay(10 + j * 10);
        monitor_reset();
    }
    monitor_enable(0);
    Sleep(100);
    monitor_snapshot(&s);
    if (s.active || s.wanted) {
        puts("FAIL disable");
        failures++;
    }
    // Require a real default renderer to exercise playback. Explicitly mark SKIP.
    monitor_device(L"");
    monitor_volume(0);
    monitor_enable(1);
    Sleep(1500);
    monitor_snapshot(&s);
    if (s.active) {
        for (int b = 0; b < 100; b++) {
            monitor_effect(b % 3 != 0, b % 2);
            monitor_feed(silence, 960);
            Sleep(20);
        }
        for (int i = 0; i < count && i < 3; i++) {
            monitor_device(d[i].id);
            Sleep(350);
            monitor_feed(silence, 960);
        }
        puts("PASS silent render, effect switching and endpoint selection requests");
    } else
        puts("SKIP physical playback / device hot-switch: no usable render endpoint");
    monitor_enable(0);
    monitor_flush();
    Sleep(50);
    monitor_shutdown();
    printf("RESULT=%s\n", failures ? "FAIL" : "PASS");
    return failures ? 1 : 0;
}
