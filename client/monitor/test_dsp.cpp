/* MIT: adapter-level tests, using deterministic non-personal synthetic audio. */
#include "plugin.h"
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#ifdef _WIN32
#include <windows.h>
#else
#include <dlfcn.h>
#endif
static int fails = 0;
static void check(bool ok, const char *what) {
    std::printf("%s %s\n", ok ? "PASS" : "FAIL", what);
    if (!ok)
        fails++;
}
static double energy(const int16_t *p) {
    double s = 0;
    for (int i = 0; i < 480; i++)
        s += double(p[i]) * p[i];
    return s;
}
static const MlDsp *load(const char *path) {
#ifdef _WIN32
    auto lib = LoadLibraryA(path);
    auto entry = lib ? (MlDspEntry)GetProcAddress(lib, "miclink_dsp_v1") : nullptr;
#else
    auto lib = dlopen(path, RTLD_NOW);
    auto entry = lib ? (MlDspEntry)dlsym(lib, "miclink_dsp_v1") : nullptr;
#endif
    check(entry != nullptr, path);
    if (!entry)
        return nullptr;
    check(entry(999) == nullptr, "incompatible ABI rejected");
    auto api = entry(1);
    check(api && api->size == sizeof(MlDsp), "ABI descriptor");
    return api;
}
int main(int argc, char **argv) {
    if (argc != 3) {
        std::fprintf(stderr, "usage: test_monitor_dsp RING_DLL SPEEX_DLL\n");
        return 2;
    }
    int16_t input[480], ref[480] = {}, out[480], original[480];
    auto ring = load(argv[1]);
    auto aec = load(argv[2]);
    if (!ring || !aec)
        return 1;
    auto r = ring->create();
    auto e = aec->create();
    check(r && e, "module construction");
    if (!r || !e)
        return 1;
    auto start = std::chrono::steady_clock::now();
    double before = 0, after = 0;
    for (int b = 0; b < 700; b++) {
        double gain = b < 200 ? 300 * std::pow(10., b / 200.) : 3000;
        for (int i = 0; i < 480; i++)
            input[i] =
                (int16_t)(gain * std::sin(2 * 3.141592653589793 * 997 * (b * 480 + i) / 48000.));
        std::memcpy(original, input, sizeof input);
        ring->process(r, input, ref, out);
        if (std::memcmp(input, original, sizeof input)) {
            check(false, "input isolation");
        }
        if (b > 600) {
            before += energy(input);
            after += energy(out);
        }
    }
    double cut = 10 * std::log10((before + 1) / (after + 1));
    std::printf("Ring Out sustained tone reduction: %.2f dB; notches: %d\n", cut, ring->status(r));
    check(cut > 6 && ring->status(r) > 0, "sustained feedback candidate suppressed");
    ring->reset(r);
    check(ring->status(r) == 0, "Ring Out clear releases learned filters");
    uint32_t rng = 42;
    int16_t previous[480] = {};
    before = after = 0;
    for (int b = 0; b < 2000; b++) {
        for (int i = 0; i < 480; i++) {
            rng = 1664525 * rng + 1013904223;
            ref[i] = (int16_t)(((int)(rng >> 16) - 32768) / 4);
            input[i] = (int16_t)(.65 * ref[i] + .12 * previous[i]);
        }
        std::memcpy(original, input, sizeof input);
        aec->process(e, input, ref, out);
        if (std::memcmp(original, input, sizeof input))
            fails++;
        std::memcpy(previous, ref, sizeof ref);
        if (b > 1700) {
            before += energy(input);
            after += energy(out);
        }
    }
    double erle = 10 * std::log10((before + 1) / (after + 1));
    std::printf("SpeexDSP aligned synthetic echo ERLE: %.2f dB\n", erle);
    check(erle > 12, "AEC converges with aligned playback reference");
    aec->reset(e);
    std::memset(input, 0, sizeof input);
    std::memset(ref, 0, sizeof ref);
    aec->process(e, input, ref, out);
    check(energy(out) == 0, "AEC reset and digital silence");
    for (int n = 0; n < 30; n++) {
        auto a = ring->create();
        auto b = aec->create();
        check(a && b, "repeated module lifecycle");
        if (a)
            ring->destroy(a);
        if (b)
            aec->destroy(b);
    }
    auto end = std::chrono::steady_clock::now();
    std::printf("Processed 27 s synthetic audio in %.3f s (not a device latency measurement)\n",
                std::chrono::duration<double>(end - start).count());
    ring->destroy(r);
    aec->destroy(e);
    std::printf("RESULT=%s failures=%d\n", fails ? "FAIL" : "PASS", fails);
    return fails ? 1 : 0;
}
