/* MicLink adapter MIT; SpeexDSP is BSD (see accompanying COPYING). */
#include "plugin.h"
#include <speex/speex_echo.h>
static void *make(void) {
    SpeexEchoState *s = speex_echo_state_init(480, 9600);
    int rate = 48000;
    if (s)
        speex_echo_ctl(s, SPEEX_ECHO_SET_SAMPLING_RATE, &rate);
    return s;
}
static void drop(void *p) {
    speex_echo_state_destroy((SpeexEchoState *)p);
}
static void reset(void *p) {
    speex_echo_state_reset((SpeexEchoState *)p);
}
static void process(void *p, const int16_t *in, const int16_t *ref, int16_t *out) {
    speex_echo_cancellation((SpeexEchoState *)p, in, ref, out);
}
static int status(void *p) {
    (void)p;
    return 0;
}
ML_EXPORT const MlDsp *miclink_dsp_v1(uint32_t abi) {
    static const MlDsp api = {ML_DSP_ABI, sizeof(MlDsp), "SpeexDSP AEC (experimental)",
                              make,       drop,          reset,
                              process,    status};
    return abi == ML_DSP_ABI ? &api : 0;
}
