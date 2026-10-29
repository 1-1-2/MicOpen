// Public API patterns: Microsoft Windows-universal-samples/Samples/WiFiDirect.
// Independently implemented Win32 UI + C ABI; no legacy SoftAP or tethering.
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include "direct.h"
#include <winrt/Windows.Foundation.h>
#include <winrt/Windows.Foundation.Collections.h>
#include <winrt/Windows.Devices.Enumeration.h>
#include <winrt/Windows.Devices.WiFiDirect.h>
#include <winrt/Windows.Networking.h>
#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdio>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

using namespace winrt;
using namespace Windows::Foundation;
using namespace Windows::Devices::Enumeration;
using namespace Windows::Devices::WiFiDirect;
using namespace std::chrono_literals;

namespace {
constexpr wchar_t className[] = L"MicLink.WiFiDirect.Selector.v1";
struct Picker {
    volatile LONG *cancel;
    HWND window{}, list{}, status{}, button{};
    std::mutex mutex;
    std::vector<DeviceInformation> devices;
    unsigned revision{}, drawn = ~0u;
    bool done{};
    std::atomic_bool aborted{false};
    DeviceInformation choice{nullptr};
    ULONGLONG deadline = GetTickCount64() + 120000;
};
bool cancelled(Picker const& p) {
    return InterlockedCompareExchange(p.cancel, 0, 0) != 0 || p.aborted;
}
void refresh(Picker& p) {
    std::lock_guard lock(p.mutex);
    if (p.drawn == p.revision) return;
    auto index = SendMessageW(p.list, LB_GETCURSEL, 0, 0);
    SendMessageW(p.list, LB_RESETCONTENT, 0, 0);
    for (auto const& d : p.devices) SendMessageW(p.list, LB_ADDSTRING, 0, (LPARAM)d.Name().c_str());
    if (index >= 0 && index < (LRESULT)p.devices.size()) SendMessageW(p.list, LB_SETCURSEL, index, 0);
    p.drawn = p.revision;
    SetWindowTextW(p.status, p.devices.empty()
        ? L"正在搜索… 请在手机选择无线直连并启动，保持 Wi-Fi 开启。"
        : L"选择手机的系统设备名。首次连接请在手机上确认配对。");
}
LRESULT CALLBACK windowProc(HWND w, UINT m, WPARAM a, LPARAM b) {
    auto p = reinterpret_cast<Picker*>(GetWindowLongPtrW(w, GWLP_USERDATA));
    if (m == WM_NCCREATE) {
        p = reinterpret_cast<Picker*>(reinterpret_cast<CREATESTRUCTW*>(b)->lpCreateParams);
        SetWindowLongPtrW(w, GWLP_USERDATA, (LONG_PTR)p);
    }
    if (!p) return DefWindowProcW(w,m,a,b);
    switch (m) {
    case WM_CREATE: {
        p->status = CreateWindowW(L"STATIC",L"正在初始化无线直连…",WS_CHILD|WS_VISIBLE,
            16,14,484,44,w,nullptr,nullptr,nullptr);
        p->list = CreateWindowExW(WS_EX_CLIENTEDGE,L"LISTBOX",L"",WS_CHILD|WS_VISIBLE|WS_VSCROLL|LBS_NOTIFY|WS_TABSTOP,
            16,64,484,180,w,(HMENU)100,nullptr,nullptr);
        p->button = CreateWindowW(L"BUTTON",L"连接",WS_CHILD|WS_VISIBLE|WS_TABSTOP|BS_DEFPUSHBUTTON,
            280,260,104,32,w,(HMENU)IDOK,nullptr,nullptr);
        CreateWindowW(L"BUTTON",L"取消",WS_CHILD|WS_VISIBLE|WS_TABSTOP,
            396,260,104,32,w,(HMENU)IDCANCEL,nullptr,nullptr);
        for (HWND c=GetWindow(w,GW_CHILD);c;c=GetWindow(c,GW_HWNDNEXT))
            SendMessageW(c,WM_SETFONT,(WPARAM)GetStockObject(DEFAULT_GUI_FONT),TRUE);
        EnableWindow(p->button,FALSE);
        SetTimer(w,1,100,nullptr);
        return 0;
    }
    case WM_TIMER:
        if (cancelled(*p) || GetTickCount64() >= p->deadline) {
            p->aborted=true; p->done=true; ShowWindow(w,SW_HIDE);
        } else if (!p->choice) refresh(*p);
        return 0;
    case WM_COMMAND:
        if (LOWORD(a)==IDCANCEL) { p->aborted=true; p->done=true; ShowWindow(w,SW_HIDE); }
        else if (LOWORD(a)==100 && HIWORD(a)==LBN_SELCHANGE)
            EnableWindow(p->button,SendMessageW(p->list,LB_GETCURSEL,0,0)!=LB_ERR);
        else if ((LOWORD(a)==IDOK || (LOWORD(a)==100 && HIWORD(a)==LBN_DBLCLK)) && !p->choice) {
            std::lock_guard lock(p->mutex);
            auto i=SendMessageW(p->list,LB_GETCURSEL,0,0);
            if (i>=0 && i<(LRESULT)p->devices.size()) {
                p->choice=p->devices[(size_t)i]; p->done=true;
                EnableWindow(p->list,FALSE); EnableWindow(p->button,FALSE);
                SetWindowTextW(p->status,L"正在配对与连接… 请查看手机上的系统确认提示。");
            }
        }
        return 0;
    case WM_CLOSE: p->aborted=true; p->done=true; ShowWindow(w,SW_HIDE); return 0;
    }
    return DefWindowProcW(w,m,a,b);
}
void pump(Picker& p) {
    MSG m;
    while (PeekMessageW(&m,nullptr,0,0,PM_REMOVE)) {
        if (m.message==WM_QUIT) { p.aborted=true; break; }
        if (!IsDialogMessageW(p.window,&m)) { TranslateMessage(&m); DispatchMessageW(&m); }
    }
    if (InterlockedCompareExchange(p.cancel,0,0) || GetTickCount64()>=p.deadline) p.aborted=true;
    MsgWaitForMultipleObjects(0,nullptr,FALSE,30,QS_ALLINPUT);
}
template<class Operation> auto await(Picker& p, Operation const& op) {
    while (op.Status()==AsyncStatus::Started && !cancelled(p)) pump(p);
    if (cancelled(p)) { op.Cancel(); throw hresult_canceled(); }
    return op.GetResults();
}
struct Session {
    bool apartment{};
    WiFiDirectAdvertisementPublisher publisher{nullptr};
    WiFiDirectDevice device{nullptr};
    ~Session() {
        try { if (device) device.Close(); } catch (...) {}
        try { if (publisher) publisher.Stop(); } catch (...) {}
        device=nullptr; publisher=nullptr;
        if (apartment) uninit_apartment();
    }
};
struct UiScope {
    std::shared_ptr<Picker> p;
    ~UiScope() { if (p->window) DestroyWindow(p->window); }
};
bool ipv4(std::string const& s) {
    unsigned a,b,c,d; char tail;
    return sscanf_s(s.c_str(),"%u.%u.%u.%u%c",&a,&b,&c,&d,&tail,1)==4 && a>0 && a<224 &&
        a!=127 && b<256 && c<256 && d<256;
}
}

extern "C" __declspec(dllexport) unsigned __cdecl ml_direct_abi() { return 1; }
extern "C" __declspec(dllexport) void __cdecl ml_direct_close(void* value) { delete static_cast<Session*>(value); }
extern "C" __declspec(dllexport) void* __cdecl ml_direct_open(
    volatile LONG* cancel, char* remote, int rcap, char* local, int lcap, char* error, int ecap) {
    if (!cancel || !remote || rcap<16 || !local || lcap<16 || !error || ecap<1) return nullptr;
    remote[0]=local[0]=error[0]=0;
    if (InterlockedCompareExchange(cancel,0,0)) { snprintf(error,ecap,"无线直连已取消"); return nullptr; }
    auto p=std::make_shared<Picker>(); p->cancel=cancel;
    UiScope ui{p};
    auto session=std::make_unique<Session>();
    try {
        init_apartment(apartment_type::single_threaded); session->apartment=true;
        WNDCLASSW wc{}; wc.lpfnWndProc=windowProc; wc.hInstance=GetModuleHandleW(nullptr);
        wc.lpszClassName=className; wc.hCursor=LoadCursor(nullptr,IDC_ARROW);
        wc.hbrBackground=(HBRUSH)(COLOR_WINDOW+1);
        if (!RegisterClassW(&wc) && GetLastError()!=ERROR_CLASS_ALREADY_EXISTS) throw_last_error();
        p->window=CreateWindowExW(WS_EX_APPWINDOW,className,L"MicLink · 无线直连（Wi-Fi Direct）",
            WS_OVERLAPPED|WS_CAPTION|WS_SYSMENU|WS_VISIBLE,CW_USEDEFAULT,CW_USEDEFAULT,540,350,
            nullptr,nullptr,wc.hInstance,p.get());
        if (!p->window) throw_last_error();
        // Microsoft connector sample starts an advertiser while discovering.
        // LegacySettings is explicitly disabled: no SSID/password hotspot fallback.
        session->publisher=WiFiDirectAdvertisementPublisher();
        session->publisher.Advertisement().IsAutonomousGroupOwnerEnabled(false);
        session->publisher.Advertisement().LegacySettings().IsEnabled(false);
        session->publisher.Start();
        ULONGLONG start=GetTickCount64();
        while (session->publisher.Status()!=WiFiDirectAdvertisementPublisherStatus::Started && GetTickCount64()-start<5000 && !cancelled(*p)) {
            if (session->publisher.Status()==WiFiDirectAdvertisementPublisherStatus::Aborted)
                throw hresult_error(E_FAIL,L"网卡或驱动无法启动 Wi-Fi Direct；请检查 Wi-Fi 开关及移动热点");
            pump(*p);
        }
        if (cancelled(*p)) throw hresult_canceled();
        if (session->publisher.Status()!=WiFiDirectAdvertisementPublisherStatus::Started)
            throw hresult_error(E_FAIL,L"无线直连启动超时，请检查网卡支持及 Wi-Fi 开关");
        auto watcher=DeviceInformation::CreateWatcher(WiFiDirectDevice::GetDeviceSelector(WiFiDirectDeviceSelectorType::AssociationEndpoint));
        auto added=watcher.Added(auto_revoke,[p](auto const&,DeviceInformation const& d) noexcept {
            try { std::lock_guard lock(p->mutex);
                auto i=std::find_if(p->devices.begin(),p->devices.end(),[&](auto const& v){return v.Id()==d.Id();});
                if (i==p->devices.end() && p->devices.size()<128) { p->devices.push_back(d); ++p->revision; }
            } catch (...) {}
        });
        auto removed=watcher.Removed(auto_revoke,[p](auto const&,DeviceInformationUpdate const& d) noexcept {
            try { std::lock_guard lock(p->mutex);
                std::erase_if(p->devices,[&](auto const& v){return v.Id()==d.Id();}); ++p->revision;
            } catch (...) {}
        });
        auto updated=watcher.Updated(auto_revoke,[p](auto const&,DeviceInformationUpdate const& d) noexcept {
            try { std::lock_guard lock(p->mutex);
                for (auto const& v:p->devices) if (v.Id()==d.Id()) { v.Update(d); ++p->revision; break; }
            } catch (...) {}
        });
        watcher.Start();
        while (!p->done && !cancelled(*p)) pump(*p);
        watcher.Stop();
        added.revoke(); removed.revoke(); updated.revoke();
        if (cancelled(*p) || !p->choice) throw hresult_canceled();
        p->deadline=GetTickCount64()+90000;
        WiFiDirectConnectionParameters params;
        params.GroupOwnerIntent(0); // Android owns the already-created P2P group.
        params.PreferenceOrderedConfigurationMethods().Append(WiFiDirectConfigurationMethod::PushButton);
        if (!p->choice.Pairing().IsPaired()) {
            auto custom=p->choice.Pairing().Custom();
            auto pairing=custom.PairingRequested(auto_revoke,[p](auto const&,DevicePairingRequestedEventArgs const& e) noexcept {
                // User explicitly chose this device. The phone retains system consent.
                if (!cancelled(*p) && e.PairingKind()==DevicePairingKinds::ConfirmOnly) e.Accept();
            });
            auto result=await(*p,custom.PairAsync(DevicePairingKinds::ConfirmOnly,DevicePairingProtectionLevel::Default,params));
            pairing.revoke();
            if (result.Status()!=DevicePairingResultStatus::Paired && result.Status()!=DevicePairingResultStatus::AlreadyPaired)
                throw hresult_error(E_FAIL,L"配对失败，状态="+to_hstring((int)result.Status())+L"。请在手机确认；必要时移除旧配对后重试。");
        }
        // Called on this STA window thread, with a message pump during the async wait.
        session->device=await(*p,WiFiDirectDevice::FromIdAsync(p->choice.Id(),params));
        if (!session->device) throw hresult_error(E_FAIL,L"无法建立无线直连会话");
        for (auto const& pair:session->device.GetConnectionEndpointPairs()) {
            auto r=to_string(pair.RemoteHostName().CanonicalName());
            auto l=to_string(pair.LocalHostName().CanonicalName());
            if (ipv4(r) && ipv4(l)) {
                snprintf(remote,rcap,"%s",r.c_str()); snprintf(local,lcap,"%s",l.c_str());
                return session.release();
            }
        }
        throw hresult_error(E_FAIL,L"无线直连已建立，但系统没有提供 IPv4 端点");
    } catch (hresult_canceled const&) { snprintf(error,ecap,"无线直连已取消或超时"); }
      catch (hresult_error const& e) {
        auto text=to_string(e.message()); snprintf(error,ecap,"无线直连：%s (0x%08X)",text.c_str(),(unsigned)e.code().value);
    } catch (...) { snprintf(error,ecap,"无线直连初始化失败，请检查系统及网卡支持"); }
    return nullptr;
}
