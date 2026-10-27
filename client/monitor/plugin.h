/* MicLink monitor DSP ABI v1. MIT. No Windows/CRT objects cross this boundary.
 * 48 kHz, mono, exactly 480 signed 16-bit samples per call. The reference is
 * actual previously submitted monitor playback, delayed by the host. */
#ifndef MICLINK_MONITOR_PLUGIN_H
#define MICLINK_MONITOR_PLUGIN_H
#include <stdint.h>
#define ML_DSP_ABI 1
#define ML_DSP_FRAMES 480
#ifdef _WIN32
#define ML_EXPORT __declspec(dllexport)
#else
#define ML_EXPORT __attribute__((visibility("default")))
#endif
#ifdef __cplusplus
extern "C" {
#endif
typedef struct MlDsp {
    uint32_t abi, size;
    const char *name;
    void *(*create)(void);
    void (*destroy)(void *);
    void (*reset)(void *);
    void (*process)(void *, const int16_t *input, const int16_t *reference, int16_t *output);
    int (*status)(void *); /* Ring Out: filter count; AEC: 0 */
} MlDsp;
typedef const MlDsp *(*MlDspEntry)(uint32_t);
#ifdef __cplusplus
}
#endif
#endif
