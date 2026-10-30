// Public API patterns: Microsoft Windows-universal-samples/Samples/WiFiDirect.
// Independently implemented Win32 UI + C ABI; no legacy SoftAP or tethering.
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include "direct.h"
#include "pairing_policy.h"
#include <winrt/Windows.Foundation.h>
#include <winrt/Windows.Foundation.Collections.h>
#include <winrt/Windows.Devices.Enumeration.h>
#include <winrt/Windows.Devices.WiFiDirect.h>
#include <winrt/Windows.Networking.h>
#include <dwmapi.h>
#include <appmodel.h>
#include <objbase.h>
#include <algorithm>
#include <atomic>
#include <cstdio>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <vector>
using namespace winrt;
using namespace Windows::Foundation;
using namespace Windows::Devices::Enumeration;
using namespace Windows::Devices::WiFiDirect;
namespace {
std::atomic<MicDirectLog> stageLogger{nullptr};
void emitStage(const char* text) noexcept { if (auto logger=stageLogger.load()) logger(text,0); }
constexpr wchar_t className[] = L"MicLink.WiFiDirect.Listener.v1";
std::optional<bool> aepFlag(DeviceInformation const& target,wchar_t const* key);
struct PairPrompt {
    DevicePairingRequestedEventArgs args{nullptr};
    Deferral deferral{nullptr};
    ~PairPrompt() { try { if (deferral) deferral.Complete(); } catch (...) {} }
};
struct Picker {
    volatile LONG *cancel{};
    HWND window{}, status{}, button{}, reject{}, pin{}, mode{}, owner{};
    bool directOpen{};
    ULONGLONG started=GetTickCount64();
    unsigned requestNumber{};
    MicDirectTheme theme{RGB(245,246,248),RGB(235,237,241),RGB(25,28,33),RGB(100,105,115),RGB(0,110,180),0};
    HBRUSH background{}, surface{};
    std::mutex mutex;
    std::vector<std::shared_ptr<PairPrompt>> prompts;
    std::shared_ptr<PairPrompt> pendingPin;
    std::atomic_bool done{false};
    bool incoming{}, pairing{};
    std::atomic_int decision{0};
    std::atomic_bool aborted{false};
    std::atomic_int ceremony{-1};
    std::atomic_bool accepted{false};
    std::atomic_long pairingError{0};
    wchar_t const* stage = L"初始化";
    hstring pairingState;
    ULONGLONG deadline = GetTickCount64() + 120000;
    ~Picker() {
        pendingPin.reset(); prompts.clear();
        if (background) DeleteObject(background);
        if (surface) DeleteObject(surface);
    }
};
void trace(Picker const& p, std::string const& message) {
    emitStage(("无线直连：attempt="+std::to_string(GetCurrentProcessId())+"-"+std::to_string(p.started)+" +"+std::to_string(GetTickCount64()-p.started)+"ms request="+std::to_string(p.requestNumber)+" path="+(p.directOpen?"DirectOpen":"ExplicitPair")+" "+message).c_str());
}
bool cancelled(Picker const& p) {
    return InterlockedCompareExchange(p.cancel, 0, 0) != 0 || p.aborted;
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
            p.accepted=true;
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
            14,12,width-28,82,w,nullptr,nullptr,nullptr);
        p->pin = CreateWindowExW(WS_EX_CLIENTEDGE,L"EDIT",L"",WS_CHILD|WS_TABSTOP|ES_NUMBER|ES_CENTER,
            14,164,width-28,28,w,(HMENU)101,nullptr,nullptr);
        SendMessageW(p->pin,EM_SETLIMITTEXT,8,0);
        CreateWindowW(L"STATIC",L"本轮连接流程（先选，再在手机点选电脑）",WS_CHILD|WS_VISIBLE,
            14,96,width-28,20,w,nullptr,nullptr,nullptr);
        p->mode=CreateWindowW(L"COMBOBOX",L"",WS_CHILD|WS_VISIBLE|WS_TABSTOP|CBS_DROPDOWNLIST|WS_VSCROLL,
            14,118,width-28,120,w,(HMENU)102,nullptr,nullptr);
        SendMessageW(p->mode,CB_ADDSTRING,0,(LPARAM)L"A · 显式系统配对（原流程）");
        SendMessageW(p->mode,CB_ADDSTRING,0,(LPARAM)L"B · 直接建立会话（对照流程）");
        SendMessageW(p->mode,CB_SETCURSEL,0,0);
        p->button = CreateWindowW(L"BUTTON",L"允许连接",WS_CHILD|WS_TABSTOP|BS_OWNERDRAW,
            width-282,216,82,30,w,(HMENU)IDOK,nullptr,nullptr);
        p->reject = CreateWindowW(L"BUTTON",L"拒绝",WS_CHILD|WS_TABSTOP|BS_OWNERDRAW,
            width-190,216,82,30,w,(HMENU)1001,nullptr,nullptr);
        CreateWindowW(L"BUTTON",L"停止等待",WS_CHILD|WS_VISIBLE|WS_TABSTOP|BS_OWNERDRAW,
            width-96,216,82,30,w,(HMENU)IDCANCEL,nullptr,nullptr);
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
        bool field=(HWND)b==p->pin;
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
        } else {
            try {showPrompts(*p);}
            catch (hresult_error const& e) {p->pairingError=e.code().value;p->aborted=true;p->pendingPin.reset();}
            catch (...) {p->pairingError=E_FAIL;p->aborted=true;p->pendingPin.reset();}
        }
        return 0;
    case WM_COMMAND:
        if (LOWORD(a)==IDCANCEL) { p->aborted=true;p->done=true;p->pendingPin.reset();ShowWindow(w,SW_HIDE); }
        else if (LOWORD(a)==IDOK && p->pendingPin) {
            wchar_t value[9]{};GetWindowTextW(p->pin,value,9);
            size_t n=wcslen(value);
            if ((n!=4 && n!=8) || std::any_of(value,value+n,[](wchar_t c){return c<L'0'||c>L'9';})) {
                SetWindowTextW(p->status,L"请输入手机显示的 4 位或 8 位数字 PIN。");return 0;
            }
            try {p->pendingPin->args.Accept(value);p->accepted=true;}
            catch (hresult_error const& e) {p->pairingError=e.code().value;p->aborted=true;}
            catch (...) {p->pairingError=E_FAIL;p->aborted=true;}
            p->pendingPin.reset();
            SecureZeroMemory(value,sizeof(value));SetWindowTextW(p->pin,L"");
            ShowWindow(p->pin,SW_HIDE);ShowWindow(p->button,SW_HIDE);
            SetWindowTextW(p->status,L"PIN 已提交，正在等待系统完成配对…");
        } else if (LOWORD(a)==IDOK && p->incoming) {
            p->decision=1;emitStage("无线直连：用户允许手机连接请求");
        } else if (LOWORD(a)==1001 && p->incoming) {
            p->decision=-1;emitStage("无线直连：用户拒绝手机连接请求");
        }
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
    if (cancelled(p)) {
        op.Cancel();p.pendingPin.reset();
        if (p.pairingError) throw hresult_error(hresult(p.pairingError.load()),L"提交系统配对确认失败");
        throw hresult_canceled();
    }
    return op.GetResults();
}
struct IncomingState {
    std::mutex mutex;
    bool active=true, occupied=false;
    std::atomic_long error{0};
    WiFiDirectConnectionRequest pending{nullptr};
    void shutdown() {
        WiFiDirectConnectionRequest closing{nullptr};
        {std::lock_guard lock(mutex);active=false;closing=pending;pending=nullptr;}
        // A COM call may dispatch callbacks. Never hold our queue lock here.
        try {if (closing) closing.Close();} catch (...) {}
    }
};
struct Session {
    bool apartment{};
    std::shared_ptr<IncomingState> incoming=std::make_shared<IncomingState>();
    WiFiDirectConnectionListener listener{nullptr};
    WiFiDirectConnectionListener::ConnectionRequested_revoker registration;
    WiFiDirectAdvertisementPublisher publisher{nullptr};
    WiFiDirectAdvertisementPublisher::StatusChanged_revoker publisherEvents;
    WiFiDirectDevice::ConnectionStatusChanged_revoker deviceEvents;
    WiFiDirectDevice device{nullptr};
    WiFiDirectConnectionRequest request{nullptr};
    ~Session() {
        incoming->shutdown();registration.revoke();publisherEvents.revoke();deviceEvents.revoke();
        try {if (device) device.Close();} catch (...) {}
        try {if (request) request.Close();} catch (...) {}
        try {if (publisher) publisher.Stop();} catch (...) {}
        device=nullptr;request=nullptr;listener=nullptr;publisher=nullptr;
        if (apartment) uninit_apartment();
        emitStage("无线直连：本轮直连资源已清理");
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
            x=std::clamp<LONG>(x,mi.rcWork.left,std::max(mi.rcWork.left,mi.rcWork.right-420));
            y=std::clamp<LONG>(y,mi.rcWork.top,std::max(mi.rcWork.top,mi.rcWork.bottom-300));
        }
    }
    p->window=CreateWindowExW(WS_EX_TOOLWINDOW|WS_EX_CONTROLPARENT,className,
        L"MicLink · 等待手机连接",
        WS_OVERLAPPED|WS_CAPTION|WS_SYSMENU|WS_VISIBLE,x,y,420,300,p->owner,nullptr,wc.hInstance,p.get());
    if (!p->window) throw_last_error();
    BOOL dark=p->theme.dark;DwmSetWindowAttribute(p->window,20,&dark,sizeof(dark));

}
void advertise(Session& session,Picker& p) {
    p.stage=L"启动本机直连广播";
    session.publisher=WiFiDirectAdvertisementPublisher();
    session.publisher.Advertisement().IsAutonomousGroupOwnerEnabled(true);
    session.publisher.Advertisement().ListenStateDiscoverability(WiFiDirectAdvertisementListenStateDiscoverability::Normal);
    auto methods=session.publisher.Advertisement().SupportedConfigurationMethods();
    methods.Append(WiFiDirectConfigurationMethod::PushButton);
    methods.Append(WiFiDirectConfigurationMethod::DisplayPin);
    methods.Append(WiFiDirectConfigurationMethod::ProvidePin);
    session.publisher.Advertisement().LegacySettings().IsEnabled(false);
    session.publisherEvents=session.publisher.StatusChanged(auto_revoke, [](auto const&, WiFiDirectAdvertisementPublisherStatusChangedEventArgs const& e) noexcept {
        try { emitStage(("无线直连：publisher status="+std::to_string((int)e.Status())+" error="+std::to_string((int)e.Error())).c_str()); } catch (...) {}
    });
    trace(p,"advertising autoGO=true methods=PBC,DisplayPin,ProvidePin legacy=false");
    session.publisher.Start();ULONGLONG start=GetTickCount64();
    while (session.publisher.Status()!=WiFiDirectAdvertisementPublisherStatus::Started && GetTickCount64()-start<5000 && !cancelled(p)) {
        if (session.publisher.Status()==WiFiDirectAdvertisementPublisherStatus::Aborted)
            throw hresult_error(E_FAIL,L"网卡或驱动无法启动 Wi-Fi Direct；请检查 Wi-Fi 开关及移动热点");
        pump(p);
    }
    if (cancelled(p)) throw hresult_canceled();
    if (session.publisher.Status()!=WiFiDirectAdvertisementPublisherStatus::Started)
        throw hresult_error(E_FAIL,L"无线直连启动超时，请检查网卡支持及 Wi-Fi 开关");
    emitStage("无线直连：本机广播已启动，等待手机请求");
}
bool ipv4(std::string const& s) {
    unsigned a,b,c,d;char tail;
    return sscanf_s(s.c_str(),"%u.%u.%u.%u%c",&a,&b,&c,&d,&tail,1)==4 && a>0 && a<224 && a!=127 && !(a==169 && b==254) && b<256 && c<256 && d<256;
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
std::optional<bool> aepFlag(DeviceInformation const& target,wchar_t const* key) {
    if (!target) return std::nullopt;
    auto props=target.Properties();
    if (!props.HasKey(key)) return std::nullopt;
    auto value=props.Lookup(key).try_as<IPropertyValue>();
    if (!value || value.Type()!=PropertyType::Boolean) return std::nullopt;
    return value.GetBoolean();
}
wchar_t const* flagName(std::optional<bool> flag) {
    return !flag.has_value()?L"未知":*flag?L"是":L"否";
}
DeviceInformation resolveDevice(Picker& p,DeviceInformation const& target) {
    // The inbound request supplies both an identity and its namespace (Kind).
    // Do not reinterpret a DeviceInterface ID as an AssociationEndpoint ID.
    if (target.Kind()==DeviceInformationKind::Unknown)
        return await(p,DeviceInformation::CreateFromIdAsync(target.Id(),
            {L"System.Devices.Aep.IsPaired",L"System.Devices.Aep.CanPair",L"System.Devices.Aep.IsPresent"}));
    return await(p,DeviceInformation::CreateFromIdAsync(target.Id(),
        {L"System.Devices.Aep.IsPaired",L"System.Devices.Aep.CanPair",L"System.Devices.Aep.IsPresent"},target.Kind()));
}
const wchar_t* procedureName(WiFiDirectPairingProcedure procedure) {
    return procedure==WiFiDirectPairingProcedure::GroupOwnerNegotiation?L"GroupOwnerNegotiation":L"Invitation";
}
void connectDevice(Session& session,std::shared_ptr<Picker> const& p,DeviceInformation const& target,
                   char* remote,int rcap,char* local,int lcap) {
    p->stage=L"读取设备配对信息";
    if (!target) throw hresult_error(HRESULT_FROM_WIN32(ERROR_FILE_NOT_FOUND),L"手机请求的设备记录已不可用，请让手机重新发起连接");
    p->pairing=true;p->done=false;p->deadline=GetTickCount64()+90000;
    ShowWindow(p->button,SW_HIDE);ShowWindow(p->reject,SW_HIDE);
    SetWindowTextW(p->status,L"正在配对与连接… 请查看手机上的确认提示。");
    WiFiDirectConnectionParameters params;
    params.GroupOwnerIntent(15);
    // Baseline preference, not an observation of the over-the-air stage.
    // DirectOpen does not apply these explicit pairing parameters.
    params.PreferredPairingProcedure(WiFiDirectPairingProcedure::GroupOwnerNegotiation);
    params.PreferenceOrderedConfigurationMethods().Append(WiFiDirectConfigurationMethod::PushButton);
    params.PreferenceOrderedConfigurationMethods().Append(WiFiDirectConfigurationMethod::DisplayPin);
    params.PreferenceOrderedConfigurationMethods().Append(WiFiDirectConfigurationMethod::ProvidePin);
    auto kinds=DevicePairingKinds::None;
    for (auto method:params.PreferenceOrderedConfigurationMethods())
        kinds=kinds|WiFiDirectConnectionParameters::GetDevicePairingKinds(method);
    auto paired=aepFlag(target,L"System.Devices.Aep.IsPaired");
    auto canPair=aepFlag(target,L"System.Devices.Aep.CanPair");
    bool isPaired=paired.value_or(target.Pairing().IsPaired());
    auto present=aepFlag(target,L"System.Devices.Aep.IsPresent");
    p->pairingState=hstring(L"记录类型=")+to_hstring((int)target.Kind())+L"，端点已配对="+flagName(paired)+L"，可配对="+flagName(canPair)+L"，在场="+flagName(present);
    if (!p->directOpen && !isPaired) {
        p->stage=L"系统配对";
        ULONGLONG started=GetTickCount64();
        emitStage("无线直连：系统配对开始，过程=GroupOwnerNegotiation；请按两端实际提示确认");
        auto custom=target.Pairing().Custom();
        auto pairing=custom.PairingRequested(auto_revoke,[p](auto const&,DevicePairingRequestedEventArgs const& e) noexcept {
            try {
                p->ceremony=(int)e.PairingKind();
                trace(*p,"PairingRequested kind="+std::to_string(p->ceremony.load()));
                if (cancelled(*p)) return;
                if (e.PairingKind()==DevicePairingKinds::ConfirmOnly) {e.Accept();p->accepted=true;trace(*p,"ConfirmOnly Accept submitted (system/peer result pending)");return;}
                if (e.PairingKind()==DevicePairingKinds::DisplayPin || e.PairingKind()==DevicePairingKinds::ProvidePin) {
                    auto prompt=std::make_shared<PairPrompt>();prompt->args=e;prompt->deferral=e.GetDeferral();
                    std::lock_guard lock(p->mutex);p->prompts.push_back(prompt);
                }
            } catch (hresult_error const& error) {p->pairingError=error.code().value;p->aborted=true;}
            catch (...) {p->pairingError=E_FAIL;p->aborted=true;}
        });
        auto result=await(*p,custom.PairAsync(kinds,DevicePairingProtectionLevel::Default,params));
        auto pairElapsed=GetTickCount64()-started;
        trace(*p,"PairAsync result="+std::to_string((int)result.Status())+" elapsedMs="+std::to_string(pairElapsed));
        pairing.revoke();p->pendingPin.reset();
        bool freshlyPaired=false;
        if (result.Status()==DevicePairingResultStatus::Failed) {
            p->stage=L"复核系统配对状态";
            try {
                auto fresh=resolveDevice(*p,target);
                auto freshFlag=aepFlag(fresh,L"System.Devices.Aep.IsPaired");
                freshlyPaired=freshFlag.value_or(false);
                p->pairingState=p->pairingState+L"，失败后复核已配对="+flagName(freshFlag);
            } catch (hresult_canceled const&) {throw;}
            catch (hresult_error const& error) {
                wchar_t code[12]{};swprintf_s(code,L"0x%08X",(unsigned)error.code().value);
                p->pairingState=p->pairingState+L"，设备记录复核错误="+code;
            }
        }
        p->stage=L"系统配对";
        if (!mic_direct_pairing_allows_session(result.Status(),freshlyPaired))
            throw hresult_error(E_FAIL,hstring(pairingStatus(result.Status()))+L"，状态="+to_hstring((int)result.Status())+
                L"；过程="+procedureName(params.PreferredPairingProcedure())+L"，方式="+to_hstring(p->ceremony.load())+
                L"，确认已提交="+(p->accepted.load()?L"是":L"否")+
                L"，耗时="+to_hstring(pairElapsed)+L"ms。尚未进入音频握手。");
    }
    trace(*p,p->directOpen?"DirectOpen selected before request; no explicit PairAsync this attempt":"explicit pairing conditions satisfied");
    emitStage("无线直连：正在建立会话，请查看两端实际系统提示");
    p->stage=L"建立直连会话";
    // Pairing settings were applied above. Reuse the resulting profile as in
    // Microsoft's sample, without starting a second pairing operation.
    trace(*p,"FromIdAsync begin");
    session.device=await(*p,WiFiDirectDevice::FromIdAsync(target.Id()));
    trace(*p,"FromIdAsync completed");
    if (!session.device) throw hresult_error(E_FAIL,L"无法建立无线直连会话");
    session.deviceEvents=session.device.ConnectionStatusChanged(auto_revoke, [](WiFiDirectDevice const& device, IInspectable const&) noexcept {
        try { emitStage(("无线直连：device connection status="+std::to_string((int)device.ConnectionStatus())).c_str()); } catch (...) {}
    });
    p->stage=L"读取直连地址";
    std::string lastEndpoints;
    auto addressDeadline=GetTickCount64()+10000;
    while (GetTickCount64()<addressDeadline && !cancelled(*p)) {
        if (session.device.ConnectionStatus()==WiFiDirectConnectionStatus::Connected) {
            for (auto const& pair:session.device.GetConnectionEndpointPairs()) {
                auto r=to_string(pair.RemoteHostName().CanonicalName()),l=to_string(pair.LocalHostName().CanonicalName());
                auto endpoints="endpoint local="+l+" remote="+r;
                if (endpoints!=lastEndpoints) { trace(*p,endpoints);lastEndpoints=endpoints; }
                if (ipv4(r) && ipv4(l)) {emitStage("无线直连：IPv4 端点就绪，继续音频握手");snprintf(remote,rcap,"%s",r.c_str());snprintf(local,lcap,"%s",l.c_str());return;}
            }
        }
        pump(*p);
    }
    if (cancelled(*p)) throw hresult_canceled();
    throw hresult_error(E_FAIL,L"未取得已连接的 IPv4 端点，请检查手机是否完成入组和地址分配");
}
void report(Picker const& p,char* error,int cap) noexcept {
    try {throw;} catch (hresult_canceled const&) {snprintf(error,cap,"无线直连已取消或超时");}
    catch (hresult_error const& e) {
        auto text=to_string(e.message()),stage=to_string(p.stage),state=to_string(p.pairingState);
        char const* hint=e.code()==HRESULT_FROM_WIN32(ERROR_FILE_NOT_FOUND)
            ? "；设备记录或关联可能已失效，请让手机重新发起连接" : "";
        snprintf(error,cap,"无线直连[%s]：%s%s%s%s (0x%08X)",stage.c_str(),text.c_str(),hint,
            state.empty()?"":"；",state.c_str(),(unsigned)e.code().value);
    }
    catch (...) {snprintf(error,cap,"无线直连初始化失败，请检查系统及网卡支持");}
}
}
extern "C" __declspec(dllexport) void __cdecl ml_direct_set_logger(MicDirectLog logger) {stageLogger=logger;}
extern "C" __declspec(dllexport) unsigned __cdecl ml_direct_abi() {return MIC_DIRECT_ABI;}
extern "C" __declspec(dllexport) void __cdecl ml_direct_close(void* value) {delete static_cast<Session*>(value);}
extern "C" __declspec(dllexport) void* __cdecl ml_direct_listen(volatile LONG* cancel,HWND owner,
    MicDirectTheme const* theme,char* remote,int rcap,char* local,int lcap,char* error,int cap) {
    if (!cancel || !remote || rcap<16 || !local || lcap<16 || !error || cap<1) return nullptr;
    remote[0]=local[0]=error[0]=0;
    if (InterlockedCompareExchange(cancel,0,0)) {snprintf(error,cap,"无线直连已取消");return nullptr;}
    auto session=std::make_unique<Session>();auto p=std::make_shared<Picker>();
    p->cancel=cancel;p->owner=owner;p->deadline=~0ull;if (theme) p->theme=*theme;
    UiScope ui{p};
    try {
        init_apartment(apartment_type::single_threaded);session->apartment=true;
        createPicker(p);
        UINT32 packageLength=0;
        auto identity=GetCurrentPackageFullName(&packageLength,nullptr);
        APTTYPE apartment;APTTYPEQUALIFIER qualifier;
        auto apartmentResult=CoGetApartmentType(&apartment,&qualifier);
        trace(*p,"context thread="+std::to_string(GetCurrentThreadId())+" apartment="+(SUCCEEDED(apartmentResult)?std::to_string((int)apartment):"unknown")+" package="+(identity==APPMODEL_ERROR_NO_PACKAGE?"none":"present-or-error"));
        session->listener=WiFiDirectConnectionListener();
        auto incoming=session->incoming;
        session->registration=session->listener.ConnectionRequested(auto_revoke,
            [incoming](auto const&,WiFiDirectConnectionRequestedEventArgs const& e) noexcept {
                try {
                    auto request=e.GetConnectionRequest();
                    bool reject;
                    {std::lock_guard lock(incoming->mutex);
                        reject=!incoming->active || incoming->occupied;
                        if (!reject) {incoming->occupied=true;incoming->pending=request;}}
                    if (reject) {
                        emitStage("无线直连：额外入站请求已关闭（会话已占用或正在停止）");
                        request.Close();
                    } else emitStage("无线直连：ConnectionRequested 回调已接收入站请求");
                } catch (hresult_error const& e) {incoming->error=e.code().value;}
                catch (...) {incoming->error=E_FAIL;}
            });
        advertise(*session,*p);
        wchar_t computer[128]{};DWORD length=128;GetComputerNameW(computer,&length);
        auto waiting=hstring(L"本机：")+computer+L"\n请在手机启动无线直连，点选此电脑。\n等待手机发起连接…";
        SetWindowTextW(p->status,waiting.c_str());p->stage=L"等待手机请求";
        while (!cancelled(*p)) {
            if (session->publisher.Status()!=WiFiDirectAdvertisementPublisherStatus::Started)
                throw hresult_error(E_FAIL,L"电脑直连广播已中断，请检查 Wi-Fi 开关及网卡驱动");
            if (incoming->error) throw hresult_error(hresult(incoming->error.load()),L"读取手机连接请求失败");
            {std::lock_guard lock(incoming->mutex);
                if (incoming->pending) {session->request=incoming->pending;incoming->pending=nullptr;}}
            if (!session->request) {pump(*p);continue;}
            emitStage("无线直连：收到手机连接请求，等待本机允许");
            p->incoming=true;p->decision=0;
            p->requestNumber++;
            p->directOpen=SendMessageW(p->mode,CB_GETCURSEL,0,0)==1;
            EnableWindow(p->mode,FALSE);
            trace(*p,"incoming request admitted; mode frozen until this attempt ends");
            trace(*p,p->directOpen?"effective session parameters=system defaults; no explicit PairAsync parameters":"explicit parameters intent=15 preferred=GroupOwnerNegotiation methods=PBC,DisplayPin,ProvidePin");
            auto target=session->request.DeviceInformation();
            auto name=target.Name();if (name.empty()) name=L"未命名手机";
            auto prompt=hstring(L"允许这台手机连接？\n")+name+L"\n请核对手机名称后选择允许。";
            SetWindowTextW(p->status,prompt.c_str());
            ShowWindow(p->button,SW_SHOW);EnableWindow(p->button,TRUE);ShowWindow(p->reject,SW_SHOW);
            p->deadline=GetTickCount64()+90000;
            while (!p->decision && !cancelled(*p)) pump(*p);
            if (cancelled(*p)) throw hresult_canceled();
            p->incoming=false;
            if (p->decision<0) {
                session->request.Close();session->request=nullptr;
                {std::lock_guard lock(incoming->mutex);incoming->occupied=false;}
                EnableWindow(p->mode,TRUE);
                p->deadline=~0ull;ShowWindow(p->button,SW_HIDE);ShowWindow(p->reject,SW_HIDE);
                SetWindowTextW(p->status,waiting.c_str());continue;
            }
            connectDevice(*session,p,target,remote,rcap,local,lcap);
            // Keep exactly one admitted phone. Reject additional requests;
            // the session and advertisement live until transport cleanup.
            return session.release();
        }
        throw hresult_canceled();
    } catch (...) {report(*p,error,cap);return nullptr;}
}
#ifdef MICLINK_DIRECT_TEST
extern "C" __declspec(dllexport) int __cdecl ml_direct_test_pairing_policy(int status,int fresh) {
    return mic_direct_pairing_allows_session(static_cast<DevicePairingResultStatus>(status),fresh!=0);
}
extern "C" __declspec(dllexport) int __cdecl ml_direct_test_ipv4(const char* address) { return address && ipv4(address); }
// Native window/pump/cancellation fixture; no radio required.
extern "C" __declspec(dllexport) int __cdecl ml_direct_test_wait(volatile LONG* cancel,unsigned timeout,unsigned mode) {
    auto p=std::make_shared<Picker>();p->cancel=cancel;UiScope ui{p};
    try {
        if (mode&1) p->theme={RGB(28,31,36),RGB(20,23,28),RGB(235,238,242),RGB(155,160,169),RGB(100,180,240),1};
        p->deadline=GetTickCount64()+timeout;createPicker(p);
        if (mode&2) {
            p->incoming=true;SetWindowTextW(p->status,L"允许这台手机连接？\nFixture phone");
            ShowWindow(p->button,SW_SHOW);EnableWindow(p->button,TRUE);ShowWindow(p->reject,SW_SHOW);
        }

        if (mode&4) {
            ShowWindow(p->pin,SW_SHOW);
            SendMessageW(p->pin,EM_SETREADONLY,TRUE,0);
            SetWindowTextW(p->pin,L"01234567");
        }
        while (!p->done && !cancelled(*p) && !p->decision) pump(*p);
        if (cancelled(*p)) return 1;
        if (mode&8) return p->decision>0 ? (SendMessageW(p->mode,CB_GETCURSEL,0,0)==1?4:2) : 3;
        return p->decision>0?2:3;}
    catch (...) {return -1;}
}
#endif
