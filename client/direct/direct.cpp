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
#include <dwmapi.h>
#include <algorithm>
#include <atomic>
#include <cstdio>
#include <memory>
#include <mutex>
#include <string>
#include <vector>
using namespace winrt;
using namespace Windows::Foundation;
using namespace Windows::Devices::Enumeration;
using namespace Windows::Devices::WiFiDirect;
namespace {
constexpr wchar_t className[] = L"MicLink.WiFiDirect.Selector.v1";
struct PairPrompt {
    DevicePairingRequestedEventArgs args{nullptr};
    Deferral deferral{nullptr};
    ~PairPrompt() { try { if (deferral) deferral.Complete(); } catch (...) {} }
};
struct Picker {
    volatile LONG *cancel{};
    HWND window{}, list{}, status{}, button{}, pin{}, owner{};
    MicDirectTheme theme{RGB(245,246,248),RGB(235,237,241),RGB(25,28,33),RGB(100,105,115),RGB(0,110,180),0};
    HBRUSH background{}, surface{};
    std::mutex mutex;
    std::vector<DeviceInformation> devices;
    std::vector<std::shared_ptr<PairPrompt>> prompts;
    std::shared_ptr<PairPrompt> pendingPin;
    unsigned revision{}, drawn = ~0u;
    std::vector<hstring> drawnIds;
    std::atomic_bool done{false};
    bool compact{}, pairing{};
    std::atomic_bool aborted{false};
    std::atomic_int ceremony{-1};
    DeviceInformation choice{nullptr};
    ULONGLONG deadline = GetTickCount64() + 120000;
    ~Picker() {
        pendingPin.reset(); prompts.clear();
        if (background) DeleteObject(background);
        if (surface) DeleteObject(surface);
    }
};
bool cancelled(Picker const& p) {
    return InterlockedCompareExchange(p.cancel, 0, 0) != 0 || p.aborted;
}
void selectDevice(Picker& p) {
    std::lock_guard lock(p.mutex);
    auto i=SendMessageW(p.list,LB_GETCURSEL,0,0);
    if (i<0 || i>=(LRESULT)p.drawnIds.size()) return;
    auto selected=std::find_if(p.devices.begin(),p.devices.end(),[&](auto const& d){return d.Id()==p.drawnIds[(size_t)i];});
    if (selected==p.devices.end()) return;
    p.choice=*selected; p.done=true;
}
void refresh(Picker& p) {
    std::lock_guard lock(p.mutex);
    if (p.pairing || p.drawn == p.revision) return;
    auto index = SendMessageW(p.list, LB_GETCURSEL, 0, 0);
    hstring selected = index >= 0 && index < (LRESULT)p.drawnIds.size() ? p.drawnIds[(size_t)index] : hstring{};
    p.drawnIds.clear();
    SendMessageW(p.list, LB_RESETCONTENT, 0, 0);
    for (auto const& d : p.devices) {
        auto i = p.drawnIds.size(); p.drawnIds.push_back(d.Id());
        SendMessageW(p.list, LB_ADDSTRING, 0, (LPARAM)d.Name().c_str());
        if (d.Id()==selected) SendMessageW(p.list, LB_SETCURSEL, i, 0);
    }
    EnableWindow(p.button,SendMessageW(p.list,LB_GETCURSEL,0,0)!=LB_ERR);
    p.drawn = p.revision;
    SetWindowTextW(p.status, p.devices.empty()
        ? L"正在搜索附近设备…\n请在手机选择无线直连并启动。"
        : L"点击手机名称完成选择。\n首次连接时还需在手机确认配对。");
}
void showPrompts(Picker& p) {
    std::vector<std::shared_ptr<PairPrompt>> incoming;
    { std::lock_guard lock(p.mutex); incoming.swap(p.prompts); }
    for (auto const& prompt:incoming) {
        if (cancelled(p)) continue;
        auto kind=prompt->args.PairingKind();
        if (kind==DevicePairingKinds::DisplayPin) {
            auto text=L"请在手机上输入此配对 PIN："+prompt->args.Pin()+L"\n确认后继续；此 PIN 不写入日志。";
            SetWindowTextW(p.status,text.c_str());
            prompt->args.Accept();
        } else if (kind==DevicePairingKinds::ProvidePin) {
            p.pendingPin=prompt;
            ShowWindow(p.pin,SW_SHOW); ShowWindow(p.button,SW_SHOW);
            SetWindowTextW(p.button,L"确认 PIN"); EnableWindow(p.button,TRUE);
            SetWindowTextW(p.status,L"请输入手机上显示的配对 PIN，然后点击确认。");
            SetFocus(p.pin);
        }
    }
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
        RECT rc{};GetClientRect(w,&rc);int width=rc.right;
        p->status = CreateWindowW(L"STATIC",L"正在初始化无线直连…",WS_CHILD|WS_VISIBLE,
            14,12,width-28,52,w,nullptr,nullptr,nullptr);
        p->list = CreateWindowExW(WS_EX_CLIENTEDGE,L"LISTBOX",L"",WS_CHILD|WS_VISIBLE|WS_VSCROLL|LBS_NOTIFY|WS_TABSTOP,
            14,70,width-28,150,w,(HMENU)100,nullptr,nullptr);
        p->pin = CreateWindowExW(WS_EX_CLIENTEDGE,L"EDIT",L"",WS_CHILD|WS_TABSTOP|ES_NUMBER|ES_CENTER,
            14,100,width-28,28,w,(HMENU)101,nullptr,nullptr);
        SendMessageW(p->pin,EM_SETLIMITTEXT,8,0);
        p->button = CreateWindowW(L"BUTTON",L"选择",WS_CHILD|WS_VISIBLE|WS_TABSTOP|BS_OWNERDRAW,
            width-190,234,80,30,w,(HMENU)IDOK,nullptr,nullptr);
        CreateWindowW(L"BUTTON",L"取消",WS_CHILD|WS_VISIBLE|WS_TABSTOP|BS_OWNERDRAW,
            width-96,234,82,30,w,(HMENU)IDCANCEL,nullptr,nullptr);
        for (HWND c=GetWindow(w,GW_CHILD);c;c=GetWindow(c,GW_HWNDNEXT))
            SendMessageW(c,WM_SETFONT,(WPARAM)GetStockObject(DEFAULT_GUI_FONT),TRUE);
        EnableWindow(p->button,FALSE);
        SetTimer(w,1,100,nullptr);
        return 0;
    }
    case WM_ERASEBKGND: {
        RECT rc{};GetClientRect(w,&rc);FillRect((HDC)a,&rc,p->background);return 1;
    }
    case WM_CTLCOLORSTATIC:
    case WM_CTLCOLOREDIT:
    case WM_CTLCOLORLISTBOX:
    case WM_CTLCOLORBTN: {
        HDC dc=(HDC)a;
        bool field=(HWND)b==p->pin || (HWND)b==p->list;
        SetTextColor(dc,p->theme.text);SetBkColor(dc,field?p->theme.surface:p->theme.background);
        return (LRESULT)(field?p->surface:p->background);
    }
    case WM_DRAWITEM: {
        auto item=reinterpret_cast<DRAWITEMSTRUCT*>(b);
        if (item->CtlType!=ODT_BUTTON) break;
        FillRect(item->hDC,&item->rcItem,p->surface);
        FrameRect(item->hDC,&item->rcItem,p->background);
        wchar_t label[32]{};GetWindowTextW(item->hwndItem,label,32);
        SetBkMode(item->hDC,TRANSPARENT);
        SetTextColor(item->hDC,(item->itemState&ODS_DISABLED)?p->theme.muted:p->theme.text);
        DrawTextW(item->hDC,label,-1,&item->rcItem,DT_CENTER|DT_VCENTER|DT_SINGLELINE);
        if (item->itemState&ODS_FOCUS) {RECT focus=item->rcItem;InflateRect(&focus,-3,-3);DrawFocusRect(item->hDC,&focus);}
        return TRUE;
    }
    case WM_TIMER:
        if (cancelled(*p) || GetTickCount64() >= p->deadline) {
            p->aborted=true;p->done=true;p->pendingPin.reset();ShowWindow(w,SW_HIDE);
        } else { showPrompts(*p);if (!p->choice) refresh(*p); }
        return 0;
    case WM_COMMAND:
        if (LOWORD(a)==IDCANCEL) { p->aborted=true;p->done=true;p->pendingPin.reset();ShowWindow(w,SW_HIDE); }
        else if (LOWORD(a)==IDOK && p->pendingPin) {
            wchar_t value[9]{};GetWindowTextW(p->pin,value,9);
            size_t n=wcslen(value);
            if ((n!=4 && n!=8) || std::any_of(value,value+n,[](wchar_t c){return c<L'0'||c>L'9';})) {
                SetWindowTextW(p->status,L"请输入手机显示的 4 位或 8 位数字 PIN。");return 0;
            }
            p->pendingPin->args.Accept(value);p->pendingPin.reset();
            SecureZeroMemory(value,sizeof(value));SetWindowTextW(p->pin,L"");
            ShowWindow(p->pin,SW_HIDE);ShowWindow(p->button,SW_HIDE);
            SetWindowTextW(p->status,L"PIN 已提交，正在等待系统完成配对…");
        } else if (LOWORD(a)==100 && HIWORD(a)==LBN_SELCHANGE) {
            if (p->compact) selectDevice(*p);
            else EnableWindow(p->button,SendMessageW(p->list,LB_GETCURSEL,0,0)!=LB_ERR);
        } else if (!p->pairing && (LOWORD(a)==IDOK || (LOWORD(a)==100 && HIWORD(a)==LBN_DBLCLK))) selectDevice(*p);
        return 0;
    case WM_CLOSE:p->aborted=true;p->done=true;p->pendingPin.reset();ShowWindow(w,SW_HIDE);return 0;
    }
    return DefWindowProcW(w,m,a,b);
}
void pump(Picker& p) {
    MSG m;
    while (PeekMessageW(&m,nullptr,0,0,PM_REMOVE)) {
        if (m.message==WM_QUIT) {p.aborted=true;break;}
        if (!IsDialogMessageW(p.window,&m)) {TranslateMessage(&m);DispatchMessageW(&m);}
    }
    if (InterlockedCompareExchange(p.cancel,0,0) || GetTickCount64()>=p.deadline) p.aborted=true;
    MsgWaitForMultipleObjects(0,nullptr,FALSE,30,QS_ALLINPUT);
}
template<class Operation> auto await(Picker& p, Operation const& op) {
    while (op.Status()==AsyncStatus::Started && !cancelled(p)) pump(p);
    if (cancelled(p)) {op.Cancel();p.pendingPin.reset();throw hresult_canceled();}
    return op.GetResults();
}
struct Session {
    bool apartment{};
    WiFiDirectAdvertisementPublisher publisher{nullptr};
    WiFiDirectDevice device{nullptr};
    ~Session() {
        try {if (device) device.Close();} catch (...) {}
        try {if (publisher) publisher.Stop();} catch (...) {}
        device=nullptr;publisher=nullptr;
        if (apartment) uninit_apartment();
    }
};
struct UiScope {
    std::shared_ptr<Picker> p;
    ~UiScope() {if (p->window) DestroyWindow(p->window);}
};
void createPicker(std::shared_ptr<Picker> const& p,int x=CW_USEDEFAULT,int y=CW_USEDEFAULT) {
    p->background=CreateSolidBrush(p->theme.background);p->surface=CreateSolidBrush(p->theme.surface);
    WNDCLASSW wc{};wc.lpfnWndProc=windowProc;wc.hInstance=GetModuleHandleW(nullptr);
    wc.lpszClassName=className;wc.hCursor=LoadCursor(nullptr,IDC_ARROW);
    if (!RegisterClassW(&wc) && GetLastError()!=ERROR_CLASS_ALREADY_EXISTS) throw_last_error();
    if (x!=CW_USEDEFAULT) {
        POINT point{x,y};MONITORINFO mi{sizeof(mi)};
        if (GetMonitorInfoW(MonitorFromPoint(point,MONITOR_DEFAULTTONEAREST),&mi)) {
            x=std::clamp<LONG>(x,mi.rcWork.left,std::max(mi.rcWork.left,mi.rcWork.right-380));
            y=std::clamp<LONG>(y,mi.rcWork.top,std::max(mi.rcWork.top,mi.rcWork.bottom-310));
        }
    }
    p->window=CreateWindowExW(WS_EX_TOOLWINDOW|WS_EX_CONTROLPARENT,className,
        p->pairing?L"MicLink · 配对手机":L"MicLink · 选择直连手机",
        WS_OVERLAPPED|WS_CAPTION|WS_SYSMENU|WS_VISIBLE,x,y,380,310,p->owner,nullptr,wc.hInstance,p.get());
    if (!p->window) throw_last_error();
    BOOL dark=p->theme.dark;DwmSetWindowAttribute(p->window,20,&dark,sizeof(dark));
    if (p->compact) ShowWindow(p->button,SW_HIDE);
    if (p->pairing) {
        ShowWindow(p->list,SW_HIDE);ShowWindow(p->button,SW_HIDE);
        SetWindowTextW(p->status,L"正在配对与连接… 请查看手机上的确认提示。");
    }
}
void advertise(Session& session,Picker& p) {
    session.publisher=WiFiDirectAdvertisementPublisher();
    session.publisher.Advertisement().IsAutonomousGroupOwnerEnabled(false);
    session.publisher.Advertisement().LegacySettings().IsEnabled(false);
    session.publisher.Start();ULONGLONG start=GetTickCount64();
    while (session.publisher.Status()!=WiFiDirectAdvertisementPublisherStatus::Started && GetTickCount64()-start<5000 && !cancelled(p)) {
        if (session.publisher.Status()==WiFiDirectAdvertisementPublisherStatus::Aborted)
            throw hresult_error(E_FAIL,L"网卡或驱动无法启动 Wi-Fi Direct；请检查 Wi-Fi 开关及移动热点");
        pump(p);
    }
    if (cancelled(p)) throw hresult_canceled();
    if (session.publisher.Status()!=WiFiDirectAdvertisementPublisherStatus::Started)
        throw hresult_error(E_FAIL,L"无线直连启动超时，请检查网卡支持及 Wi-Fi 开关");
}
DeviceInformation discover(std::shared_ptr<Picker> const& p) {
    auto watcher=DeviceInformation::CreateWatcher(WiFiDirectDevice::GetDeviceSelector(WiFiDirectDeviceSelectorType::AssociationEndpoint));
    auto added=watcher.Added(auto_revoke,[p](auto const&,DeviceInformation const& d) noexcept {
        try {std::lock_guard lock(p->mutex);
            auto i=std::find_if(p->devices.begin(),p->devices.end(),[&](auto const& v){return v.Id()==d.Id();});
            if (i==p->devices.end() && p->devices.size()<128) {p->devices.push_back(d);++p->revision;}
        } catch (...) {}
    });
    auto removed=watcher.Removed(auto_revoke,[p](auto const&,DeviceInformationUpdate const& d) noexcept {
        try {std::lock_guard lock(p->mutex);std::erase_if(p->devices,[&](auto const& v){return v.Id()==d.Id();});++p->revision;} catch (...) {}
    });
    auto updated=watcher.Updated(auto_revoke,[p](auto const&,DeviceInformationUpdate const& d) noexcept {
        try {std::lock_guard lock(p->mutex);for (auto const& v:p->devices) if (v.Id()==d.Id()) {v.Update(d);++p->revision;break;}} catch (...) {}
    });
    auto stopped=watcher.Stopped(auto_revoke,[p](auto const&,auto const&) noexcept {
        if (!p->done) p->aborted=true;
    });
    watcher.Start();
    while (!p->done && !cancelled(*p)) pump(*p);
    // Set before Stop so the Stopped callback never cancels a valid selection.
    p->done=true;watcher.Stop();
    if (cancelled(*p) || !p->choice) throw hresult_canceled();
    return p->choice;
}
bool ipv4(std::string const& s) {
    unsigned a,b,c,d;char tail;
    return sscanf_s(s.c_str(),"%u.%u.%u.%u%c",&a,&b,&c,&d,&tail,1)==4 && a>0 && a<224 && a!=127 && b<256 && c<256 && d<256;
}
const wchar_t* pairingStatus(DevicePairingResultStatus status) {
    switch (status) {
    case DevicePairingResultStatus::Failed:return L"未知配对失败";
    case DevicePairingResultStatus::AuthenticationTimeout:return L"配对认证超时";
    case DevicePairingResultStatus::ConnectionRejected:return L"对端拒绝连接";
    case DevicePairingResultStatus::AuthenticationFailure:return L"配对认证失败";
    case DevicePairingResultStatus::RequiredHandlerNotRegistered:return L"配对方式不受支持";
    case DevicePairingResultStatus::OperationAlreadyInProgress:return L"系统已有配对操作正在进行";
    case DevicePairingResultStatus::RemoteDeviceHasAssociation:return L"对端已有连接关联";
    default:return L"系统未完成配对";
    }
}
void connectDevice(Session& session,std::shared_ptr<Picker> const& p,DeviceInformation const& target,
                   char* remote,int rcap,char* local,int lcap) {
    p->pairing=true;p->done=false;p->deadline=GetTickCount64()+90000;
    ShowWindow(p->list,SW_HIDE);ShowWindow(p->button,SW_HIDE);
    SetWindowTextW(p->status,L"正在配对与连接… 请查看手机上的确认提示。");
    WiFiDirectConnectionParameters params;
    params.GroupOwnerIntent(0);
    // Android has already created its group. Invitation lets the accepting GO
    // choose a supported WPS ceremony instead of forcing GO negotiation/PBC.
    params.PreferredPairingProcedure(WiFiDirectPairingProcedure::Invitation);
    params.PreferenceOrderedConfigurationMethods().Append(WiFiDirectConfigurationMethod::PushButton);
    params.PreferenceOrderedConfigurationMethods().Append(WiFiDirectConfigurationMethod::DisplayPin);
    params.PreferenceOrderedConfigurationMethods().Append(WiFiDirectConfigurationMethod::ProvidePin);
    auto kinds=DevicePairingKinds::None;
    for (auto method:params.PreferenceOrderedConfigurationMethods())
        kinds=kinds|WiFiDirectConnectionParameters::GetDevicePairingKinds(method);
    if (!target.Pairing().IsPaired()) {
        auto custom=target.Pairing().Custom();
        auto pairing=custom.PairingRequested(auto_revoke,[p](auto const&,DevicePairingRequestedEventArgs const& e) noexcept {
            try {
                p->ceremony=(int)e.PairingKind();
                if (cancelled(*p)) return;
                if (e.PairingKind()==DevicePairingKinds::ConfirmOnly) {e.Accept();return;}
                if (e.PairingKind()==DevicePairingKinds::DisplayPin || e.PairingKind()==DevicePairingKinds::ProvidePin) {
                    auto prompt=std::make_shared<PairPrompt>();prompt->args=e;prompt->deferral=e.GetDeferral();
                    std::lock_guard lock(p->mutex);p->prompts.push_back(prompt);
                }
            } catch (...) {}
        });
        auto result=await(*p,custom.PairAsync(kinds,DevicePairingProtectionLevel::Default,params));
        pairing.revoke();p->pendingPin.reset();
        if (result.Status()!=DevicePairingResultStatus::Paired && result.Status()!=DevicePairingResultStatus::AlreadyPaired)
            throw hresult_error(E_FAIL,hstring(pairingStatus(result.Status()))+L"，状态="+to_hstring((int)result.Status())+
                L"；过程=Invitation，方式="+to_hstring(p->ceremony.load()));
    }
    session.device=await(*p,WiFiDirectDevice::FromIdAsync(target.Id(),params));
    if (!session.device) throw hresult_error(E_FAIL,L"无法建立无线直连会话");
    for (auto const& pair:session.device.GetConnectionEndpointPairs()) {
        auto r=to_string(pair.RemoteHostName().CanonicalName()),l=to_string(pair.LocalHostName().CanonicalName());
        if (ipv4(r) && ipv4(l)) {snprintf(remote,rcap,"%s",r.c_str());snprintf(local,lcap,"%s",l.c_str());return;}
    }
    throw hresult_error(E_FAIL,L"无线直连已建立，但系统没有提供 IPv4 端点");
}
void report(char* error,int cap) noexcept {
    try {throw;} catch (hresult_canceled const&) {snprintf(error,cap,"无线直连已取消或超时");}
    catch (hresult_error const& e) {auto text=to_string(e.message());snprintf(error,cap,"无线直连：%s (0x%08X)",text.c_str(),(unsigned)e.code().value);}
    catch (...) {snprintf(error,cap,"无线直连初始化失败，请检查系统及网卡支持");}
}
}
extern "C" __declspec(dllexport) unsigned __cdecl ml_direct_abi() {return MIC_DIRECT_ABI;}
extern "C" __declspec(dllexport) void __cdecl ml_direct_close(void* value) {delete static_cast<Session*>(value);}
extern "C" __declspec(dllexport) int __cdecl ml_direct_pick(volatile LONG* cancel,HWND owner,int x,int y,
    MicDirectTheme const* theme,MicDirectDevice* output,char* error,int cap) {
    if (!cancel || !output || !error || cap<1) return 0;
    *output={};error[0]=0;
    if (InterlockedCompareExchange(cancel,0,0)) {snprintf(error,cap,"无线直连已取消");return 0;}
    auto session=std::make_unique<Session>();auto p=std::make_shared<Picker>();
    p->cancel=cancel;p->owner=owner;p->compact=true;if (theme) p->theme=*theme;
    UiScope ui{p};
    try {
        init_apartment(apartment_type::single_threaded);session->apartment=true;
        createPicker(p,x,y);advertise(*session,*p);auto device=discover(p);
        auto id=to_string(device.Id());
        if (id.size()>=sizeof(output->id)) throw hresult_error(E_BOUNDS,L"设备标识过长，无法选择");
        snprintf(output->id,sizeof(output->id),"%s",id.c_str());
        wcsncpy_s(output->name,device.Name().c_str(),_TRUNCATE);return 1;
    } catch (...) {report(error,cap);return 0;}
}
extern "C" __declspec(dllexport) void* __cdecl ml_direct_connect(volatile LONG* cancel,char const* id,HWND owner,
    MicDirectTheme const* theme,char* remote,int rcap,char* local,int lcap,char* error,int cap) {
    if (!cancel || !id || !*id || !remote || rcap<16 || !local || lcap<16 || !error || cap<1) return nullptr;
    remote[0]=local[0]=error[0]=0;
    if (InterlockedCompareExchange(cancel,0,0)) {snprintf(error,cap,"无线直连已取消");return nullptr;}
    auto session=std::make_unique<Session>();auto p=std::make_shared<Picker>();
    p->cancel=cancel;p->owner=owner;p->pairing=true;if (theme) p->theme=*theme;
    UiScope ui{p};
    try {
        init_apartment(apartment_type::single_threaded);session->apartment=true;
        createPicker(p);advertise(*session,*p);
        auto target=await(*p,DeviceInformation::CreateFromIdAsync(to_hstring(id),nullptr,DeviceInformationKind::AssociationEndpoint));
        connectDevice(*session,p,target,remote,rcap,local,lcap);return session.release();
    } catch (...) {report(error,cap);return nullptr;}
}
extern "C" __declspec(dllexport) void* __cdecl ml_direct_open(volatile LONG* cancel,
    char* remote,int rcap,char* local,int lcap,char* error,int cap) {
    if (!cancel || !remote || rcap<16 || !local || lcap<16 || !error || cap<1) return nullptr;
    remote[0]=local[0]=error[0]=0;
    if (InterlockedCompareExchange(cancel,0,0)) {snprintf(error,cap,"无线直连已取消");return nullptr;}
    auto p=std::make_shared<Picker>();p->cancel=cancel;UiScope ui{p};auto session=std::make_unique<Session>();
    try {
        init_apartment(apartment_type::single_threaded);session->apartment=true;
        createPicker(p);advertise(*session,*p);auto target=discover(p);
        connectDevice(*session,p,target,remote,rcap,local,lcap);return session.release();
    } catch (...) {report(error,cap);return nullptr;}
}
#ifdef MICLINK_DIRECT_TEST
// Native window/pump/cancellation fixture; no radio required.
extern "C" __declspec(dllexport) int __cdecl ml_direct_test_wait(volatile LONG* cancel,unsigned timeout,unsigned mode) {
    auto p=std::make_shared<Picker>();p->cancel=cancel;UiScope ui{p};
    try {
        p->compact=(mode&2)!=0;
        if (mode&1) p->theme={RGB(28,31,36),RGB(20,23,28),RGB(235,238,242),RGB(155,160,169),RGB(100,180,240),1};
        p->deadline=GetTickCount64()+timeout;createPicker(p);
        if (mode&4) {
            ShowWindow(p->list,SW_HIDE);ShowWindow(p->pin,SW_SHOW);
            SendMessageW(p->pin,EM_SETREADONLY,TRUE,0);
            SetWindowTextW(p->pin,L"01234567");
        }
        while (!p->done && !cancelled(*p)) pump(*p);return cancelled(*p)?1:0;}
    catch (...) {return -1;}
}
#endif
