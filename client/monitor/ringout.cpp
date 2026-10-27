/* Adapter: GPL-3.0-or-later, because it incorporates Ring Out. */
#include "plugin.h"
#include "vendor/ringout/RingOutDSP.hpp"
#include <cmath>
#include <new>
struct Ring {
    duskaudio::RingOutDSP dsp;
    float in[480], out[480];
};
static void *make() {
    try {
        auto r = new Ring;
        r->dsp.prepare(48000, 480);
        r->dsp.setSetup(true);
        return r;
    } catch (...) {
        return nullptr;
    }
}
static void drop(void *p) {
    delete static_cast<Ring *>(p);
}
static void reset(void *p) {
    auto &d = static_cast<Ring *>(p)->dsp;
    d.resetFilters();
    d.reset();
    d.setSetup(true);
}
static void process(void *p, const int16_t *in, const int16_t *, int16_t *out) {
    auto r = static_cast<Ring *>(p);
    // Continuous automatic monitoring: rearm the upstream 60 s learn timer.
    if (!r->dsp.setupActive())
        r->dsp.setSetup(true);
    for (int i = 0; i < 480; i++)
        r->in[i] = in[i] / 32768.0f;
    const float *a[] = {r->in};
    float *b[] = {r->out};
    r->dsp.processBlock(a, b, 1, 480);
    for (int i = 0; i < 480; i++) {
        float v = r->out[i] * 32768.f;
        if (!std::isfinite(v))
            v = 0;
        out[i] = (int16_t)std::lround(v > 32767 ? 32767 : v < -32768 ? -32768 : v);
    }
}
static int status(void *p) {
    duskaudio::ringout::FilterTable t;
    static_cast<Ring *>(p)->dsp.getTable(t);
    return t.count;
}
extern "C" ML_EXPORT const MlDsp *miclink_dsp_v1(uint32_t abi) {
    static const MlDsp api = {ML_DSP_ABI, sizeof(MlDsp), "Ring Out", make,
                              drop,       reset,         process,    status};
    return abi == ML_DSP_ABI ? &api : nullptr;
}
