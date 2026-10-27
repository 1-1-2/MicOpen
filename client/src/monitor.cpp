/* MIT. WASAPI worker and narrow optional-DSP host. Never called synchronously
 * by network reception for device operations, loading, allocation or processing. */
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include "monitor.h"
#include "../monitor/plugin.h"
#include <algorithm>
#include <atomic>
#include <audioclient.h>
#include <cmath>
#include <cstring>
#include <cwchar>
#include <mmdeviceapi.h>
#include <propsys.h>

namespace {
constexpr int B = 480, Q = 4800, H = 48000;
const PROPERTYKEY friendlyName = {
    {0xa45c254e, 0xdf1c, 0x4efd, {0x80, 0x20, 0x67, 0xd1, 0x46, 0xa8, 0x50, 0xe0}}, 14};
INIT_ONCE once = INIT_ONCE_STATIC_INIT;
CRITICAL_SECTION cs;
HANDLE wake, thread;
std::atomic<bool> quit{false}, want{false};
std::atomic<int> vol{70}, fx{0}, choice{0}, delayMs{100}, generation{0}, clearEpoch{0};
std::atomic<unsigned> resetSeq[2]{};
int16_t queue[Q];
int readAt = 0, used = 0;
wchar_t requested[MON_DEVICE_ID] = {};
MonitorStatus state = {};
bool ready = false;
BOOL CALLBACK initialize(PINIT_ONCE, void *, void **) {
    InitializeCriticalSection(&cs);
    wake = CreateEventW(nullptr, FALSE, FALSE, nullptr);
    ready = wake != nullptr;
    return ready;
}
void message(const wchar_t *s) {
    EnterCriticalSection(&cs);
    lstrcpynW(state.message, s, 256);
    LeaveCriticalSection(&cs);
}
struct Plugin {
    HMODULE lib = nullptr;
    const MlDsp *api = nullptr;
    void *ctx = nullptr;
    bool load(const wchar_t *name) {
        wchar_t path[32768];
        DWORD n = GetModuleFileNameW(nullptr, path, 32768);
        if (!n || n >= 32768)
            return false;
        wchar_t *end = wcsrchr(path, L'\\');
        if (!end)
            return false;
        if ((end - path) + 1 + wcslen(name) >= 32768)
            return false;
        wcscpy(end + 1, name);
        lib = LoadLibraryExW(path, nullptr,
                             LOAD_LIBRARY_SEARCH_DLL_LOAD_DIR | LOAD_LIBRARY_SEARCH_SYSTEM32);
        if (!lib)
            return false;
        auto entry = reinterpret_cast<MlDspEntry>(GetProcAddress(lib, "miclink_dsp_v1"));
        if (entry)
            api = entry(ML_DSP_ABI);
        if (!api || api->abi != ML_DSP_ABI || api->size < sizeof(MlDsp) || !api->create ||
            !api->destroy || !api->reset || !api->process || !api->status)
            return false;
        ctx = api->create();
        return ctx != nullptr;
    }
    void reset() {
        if (ctx)
            api->reset(ctx);
    }
    ~Plugin() {
        if (ctx)
            api->destroy(ctx);
        if (lib)
            FreeLibrary(lib);
    }
};
struct Output {
    IMMDevice *device = nullptr;
    IAudioClient *client = nullptr;
    IAudioRenderClient *render = nullptr;
    UINT32 size = 0;
    wchar_t id[MON_DEVICE_ID] = {}, name[MON_DEVICE_NAME] = {};
    void close() {
        if (client)
            client->Stop();
        if (render)
            render->Release();
        if (client)
            client->Release();
        if (device)
            device->Release();
        render = nullptr;
        client = nullptr;
        device = nullptr;
    }
    ~Output() {
        close();
    }
    HRESULT open(IMMDeviceEnumerator *enumerator, const wchar_t *selected) {
        HRESULT hr = selected[0] ? enumerator->GetDevice(selected, &device)
                                 : enumerator->GetDefaultAudioEndpoint(eRender, eConsole, &device);
        if (FAILED(hr))
            return hr;
        LPWSTR did = nullptr;
        if (SUCCEEDED(device->GetId(&did))) {
            lstrcpynW(id, did, MON_DEVICE_ID);
            CoTaskMemFree(did);
        }
        IPropertyStore *store = nullptr;
        if (SUCCEEDED(device->OpenPropertyStore(STGM_READ, &store))) {
            PROPVARIANT v;
            PropVariantInit(&v);
            if (SUCCEEDED(store->GetValue(friendlyName, &v)) && v.vt == VT_LPWSTR)
                lstrcpynW(name, v.pwszVal, MON_DEVICE_NAME);
            PropVariantClear(&v);
            store->Release();
        }
        hr = device->Activate(__uuidof(IAudioClient), CLSCTX_ALL, nullptr, (void **)&client);
        if (FAILED(hr))
            return hr;
        WAVEFORMATEX format = {WAVE_FORMAT_PCM, 1, 48000, 96000, 2, 16, 0};
        hr = client->Initialize(AUDCLNT_SHAREMODE_SHARED,
                                AUDCLNT_STREAMFLAGS_AUTOCONVERTPCM |
                                    AUDCLNT_STREAMFLAGS_SRC_DEFAULT_QUALITY,
                                600000, 0, &format, nullptr);
        if (FAILED(hr))
            return hr;
        hr = client->GetBufferSize(&size);
        if (FAILED(hr))
            return hr;
        hr = client->GetService(__uuidof(IAudioRenderClient), (void **)&render);
        if (FAILED(hr))
            return hr;
        return client->Start();
    }
};
int16_t clip(float x) {
    return (int16_t)(x >= 0 ? std::min(32767.f, x) + .5f : std::max(-32768.f, x) - .5f);
}
DWORD WINAPI worker(void *) {
    HRESULT com = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
    IMMDeviceEnumerator *enumerator = nullptr;
    HRESULT hr = FAILED(com)
                     ? com
                     : CoCreateInstance(__uuidof(MMDeviceEnumerator), nullptr, CLSCTX_ALL,
                                        __uuidof(IMMDeviceEnumerator), (void **)&enumerator);
    if (FAILED(hr)) {
        message(L"无法初始化系统播放设备枚举");
        return 1;
    }
    Plugin plugins[2];
    bool available[2] = {plugins[0].load(L"miclink-ringout.dll"),
                         plugins[1].load(L"miclink-speex.dll")};
    EnterCriticalSection(&cs);
    state.ring_available = available[0];
    state.aec_available = available[1];
    LeaveCriticalSection(&cs);
    Output slots[2];
    Output *output = nullptr;
    int seen = -1, epoch = -1, fadeIn = 0;
    unsigned seenReset[2] = {resetSeq[0].load(), resetSeq[1].load()};
    DWORD poll = 0, retry = 0;
    int16_t history[H] = {}, in[B], reference[B], processed[2][B];
    int histAt = 0;
    float weights[3] = {1, 0, 0}, volume = .7f;
    auto reset = [&]() {
        memset(history, 0, sizeof(history));
        histAt = 0;
        for (auto &p : plugins)
            p.reset();
    };
    while (!quit.load()) {
        WaitForSingleObject(wake, 5);
        int gen = generation.load(), ep = clearEpoch.load();
        DWORD now = GetTickCount();
        wchar_t selected[MON_DEVICE_ID];
        EnterCriticalSection(&cs);
        lstrcpynW(selected, requested, MON_DEVICE_ID);
        LeaveCriticalSection(&cs);
        bool replace = gen != seen;
        if (output && !selected[0] && now - poll >= 1000) {
            poll = now;
            IMMDevice *d = nullptr;
            LPWSTR id = nullptr;
            if (SUCCEEDED(enumerator->GetDefaultAudioEndpoint(eRender, eConsole, &d))) {
                if (SUCCEEDED(d->GetId(&id))) {
                    replace = replace || wcscmp(id, output->id) != 0;
                    CoTaskMemFree(id);
                }
                d->Release();
            } else {
                if (output)
                    output->close();
                output = nullptr;
                message(L"系统默认输出不可用，监听已暂停");
            }
        }
        // DSP learning resets never stop the output device or discard its queue.
        for (int p = 0; p < 2; ++p) {
            unsigned revision = resetSeq[p].load();
            if (revision != seenReset[p]) {
                seenReset[p] = revision;
                plugins[p].reset();
                if (p == 0) {
                    EnterCriticalSection(&cs);
                    state.filters = available[0] ? plugins[0].api->status(plugins[0].ctx) : 0;
                    LeaveCriticalSection(&cs);
                }
            }
        }
        if (!want.load()) {
            if (output)
                output->close();
            output = nullptr;
            EnterCriticalSection(&cs);
            used = readAt = 0;
            state.active = 0;
            LeaveCriticalSection(&cs);
            seen = -1;
            continue;
        }
        if (replace || (!output && now - retry >= 1000)) {
            retry = now;
            seen = gen;
            Output *next = output == &slots[0] ? &slots[1] : &slots[0];
            next->close();
            hr = next->open(enumerator, selected);
            if (SUCCEEDED(hr)) {
                if (output)
                    output->close();
                output = next;
                fadeIn = 0;
                reset();
                EnterCriticalSection(&cs);
                used = readAt = 0;
                lstrcpynW(state.device, output->name, MON_DEVICE_NAME);
                LeaveCriticalSection(&cs);
                message(L"监听就绪；设备切换已生效");
            } else {
                next->close();
                if (output)
                    output->close();
                output = nullptr;
                wchar_t msg[256];
                swprintf(msg, 256, L"输出不可用 (0x%08lX)，监听暂停并重试；不会回退到其他扬声器",
                         (unsigned long)hr);
                message(msg);
            }
        }
        if (ep != epoch) {
            epoch = ep;
            reset();
            if (output) {
                output->client->Stop();
                output->client->Reset();
                output->client->Start();
                fadeIn = 0;
            }
        }
        EnterCriticalSection(&cs);
        state.active = output != nullptr;
        LeaveCriticalSection(&cs);
        if (!output)
            continue;
        UINT32 padding = 0;
        hr = output->client->GetCurrentPadding(&padding);
        if (FAILED(hr)) {
            if (output)
                output->close();
            output = nullptr;
            message(L"播放设备已断开，监听暂停并重试");
            continue;
        }
        if (padding + B > output->size || padding > 1440)
            continue;
        EnterCriticalSection(&cs);
        if (used < B) {
            LeaveCriticalSection(&cs);
            continue;
        }
        for (int i = 0; i < B; i++)
            in[i] = queue[(readAt + i) % Q];
        readAt = (readAt + B) % Q;
        used -= B;
        LeaveCriticalSection(&cs);
        int lag = std::max(B, delayMs.load() * 48);
        for (int i = 0; i < B; i++)
            reference[i] = history[(histAt + H - lag + i) % H];
        for (int p = 0; p < 2; p++)
            if (available[p])
                plugins[p].api->process(plugins[p].ctx, in, reference, processed[p]);
        int selectedModule = choice.load();
        int target = fx.load() && available[selectedModule] ? selectedModule + 1 : 0;
        if (fx.load() && !available[selectedModule])
            message(L"所选模块缺失或初始化失败，当前为原声旁路");
        BYTE *bytes = nullptr;
        hr = output->render->GetBuffer(B, &bytes);
        if (FAILED(hr)) {
            if (output)
                output->close();
            output = nullptr;
            message(L"播放缓冲不可用，监听暂停并重试");
            continue;
        }
        auto dst = reinterpret_cast<int16_t *>(bytes);
        for (int i = 0; i < B; i++) {
            for (int j = 0; j < 3; j++)
                weights[j] += (float(j == target) - weights[j]) / 960.f; // ~20 ms, 90% in 46 ms
            volume += (vol.load() / 100.f - volume) / 480.f;
            float y = in[i] * weights[0];
            for (int p = 0; p < 2; p++)
                if (available[p])
                    y += processed[p][i] * weights[p + 1];
            float gain = std::min(1.f, fadeIn / 960.f) * volume;
            if (fadeIn < 960)
                ++fadeIn;
            dst[i] = clip(y * gain);
            history[histAt] = dst[i];
            histAt = (histAt + 1) % H;
        }
        hr = output->render->ReleaseBuffer(B, 0);
        if (FAILED(hr)) {
            if (output)
                output->close();
            output = nullptr;
            message(L"提交播放数据失败，监听暂停并重试");
        }
        EnterCriticalSection(&cs);
        state.filters = available[0] ? plugins[0].api->status(plugins[0].ctx) : 0;
        state.switching = weights[target] < .999f;
        LeaveCriticalSection(&cs);
    }
    if (output)
        output->close();
    enumerator->Release();
    if (SUCCEEDED(com))
        CoUninitialize();
    return 0;
}
} // namespace
extern "C" {
int monitor_init(void) {
    if (!InitOnceExecuteOnce(&once, initialize, nullptr, nullptr))
        return 0;
    if (!thread) {
        quit = false;
        thread = CreateThread(nullptr, 0, worker, nullptr, 0, nullptr);
    }
    return thread != nullptr;
}
void monitor_shutdown(void) {
    if (thread) {
        quit = true;
        SetEvent(wake);
        WaitForSingleObject(thread, INFINITE);
        CloseHandle(thread);
        thread = nullptr;
    }
    if (wake) {
        CloseHandle(wake);
        wake = nullptr;
    }
}
int monitor_enable(int on) {
    if (!monitor_init())
        return 0;
    want = on != 0;
    ++generation;
    SetEvent(wake);
    return 1;
}
void monitor_feed(const int16_t *pcm, int n) {
    if (!ready || !thread || !want.load() || !pcm || n <= 0)
        return;
    if (!TryEnterCriticalSection(&cs))
        return;
    if (n > Q) {
        pcm += n - Q;
        n = Q;
    }
    // Keep at most 80 ms. Stale monitor audio is less useful than a fresh frame.
    if (used + n > 3840) {
        state.dropped_frames += (used + 479) / 480;
        used = readAt = 0;
        ++clearEpoch;
    }
    for (int i = 0; i < n; i++)
        queue[(readAt + used + i) % Q] = pcm[i];
    used += n;
    LeaveCriticalSection(&cs);
    SetEvent(wake);
}
void monitor_flush(void) {
    if (!ready)
        return;
    EnterCriticalSection(&cs);
    used = readAt = 0;
    LeaveCriticalSection(&cs);
    ++clearEpoch;
    if (wake)
        SetEvent(wake);
}
void monitor_device(const wchar_t *id) {
    if (!monitor_init())
        return;
    EnterCriticalSection(&cs);
    lstrcpynW(requested, id ? id : L"", MON_DEVICE_ID);
    LeaveCriticalSection(&cs);
    ++generation;
    SetEvent(wake);
}
void monitor_volume(int percent) {
    vol = std::max(0, std::min(100, percent));
}
void monitor_effect(int enabled, int module) {
    choice = module == 1 ? 1 : 0;
    fx = enabled != 0;
}
void monitor_delay(int ms) {
    int value = std::max(10, std::min(400, ms));
    if (delayMs.exchange(value) != value) {
        ++resetSeq[1]; // the old AEC filter was fitted to a different reference delay
        if (wake) SetEvent(wake);
    }
}
void monitor_reset(void) {
    ++resetSeq[choice.load() == 1 ? 1 : 0];
    if (wake) SetEvent(wake);
}
void monitor_snapshot(MonitorStatus *out) {
    memset(out, 0, sizeof(*out));
    if (!ready)
        return;
    EnterCriticalSection(&cs);
    *out = state;
    LeaveCriticalSection(&cs);
    out->wanted = want;
    out->volume = vol;
    out->suppression = fx;
    out->module = choice;
    out->delay_ms = delayMs;
}
int monitor_devices(MonitorDevice *out, int cap) {
    HRESULT co = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    IMMDeviceEnumerator *e = nullptr;
    IMMDeviceCollection *c = nullptr;
    int n = 0;
    if (SUCCEEDED(CoCreateInstance(__uuidof(MMDeviceEnumerator), nullptr, CLSCTX_ALL,
                                   __uuidof(IMMDeviceEnumerator), (void **)&e)) &&
        SUCCEEDED(e->EnumAudioEndpoints(eRender, DEVICE_STATE_ACTIVE, &c))) {
        UINT count = 0;
        c->GetCount(&count);
        for (UINT i = 0; i < count && n < cap; i++) {
            IMMDevice *d = nullptr;
            if (FAILED(c->Item(i, &d)))
                continue;
            LPWSTR id = nullptr;
            if (SUCCEEDED(d->GetId(&id)) && wcslen(id) < MON_DEVICE_ID) {
                lstrcpynW(out[n].id, id, MON_DEVICE_ID);
                lstrcpynW(out[n].name, L"播放设备", MON_DEVICE_NAME);
                IPropertyStore *p = nullptr;
                if (SUCCEEDED(d->OpenPropertyStore(STGM_READ, &p))) {
                    PROPVARIANT v;
                    PropVariantInit(&v);
                    if (SUCCEEDED(p->GetValue(friendlyName, &v)) && v.vt == VT_LPWSTR)
                        lstrcpynW(out[n].name, v.pwszVal, MON_DEVICE_NAME);
                    PropVariantClear(&v);
                    p->Release();
                }
                n++;
            }
            CoTaskMemFree(id);
            d->Release();
        }
    }
    if (c)
        c->Release();
    if (e)
        e->Release();
    if (SUCCEEDED(co))
        CoUninitialize();
    return n;
}
}
