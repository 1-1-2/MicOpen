# 无线直连 · 开发与实机验收

当前分支新增真正的 **Wi-Fi Direct**，尚未完成用户实机验收，**不发布新 Release**。既有 Wi-Fi、手机热点、蓝牙、USB 的实机反馈不作为新 Direct 的验收证据。

## 怎么连接

1. 手机保持 Wi-Fi 开启，选择「无线直连」，点击「启动」，授予附近 Wi-Fi 设备权限。Android 12L 及更早版本需要定位权限；系统提示时检查定位开关。不用开启手机热点。
2. 等手机显示直连已就绪及本机设备名。Windows 选择「无线直连」后，自动在鼠标旁打开搜索窗口；点击手机名称完成选择，回到主界面点击「连接」。
3. 首次连接按两端系统提示确认配对。控制端口仍应一致，默认 8125；不用填写 IP。电脑随后沿用现有音频协议连接。
4. 再次点击「无线直连」可以重新选择手机；取消搜索后保留上一次选择。断线后点击「连接」重试；不会通过自动重连反复弹出搜索窗口。更换手机连接方式前先停止。

路由器和手机热点统一在「Wi-Fi」项下，仍填写实际手机 IP。`--transport hotspot` 保留为 `wifi` 的兼容别名，不会触发 Direct。旧 Android 的 `HOTSPOT` 设置也迁移到普通 Wi-Fi。

## 实现与边界

| 部分 | 责任 |
|---|---|
| Android `WifiDirectTransport` | 检查硬件与 Wi-Fi、创建并拥有 P2P 组、观察状态、交还实际组所有者地址、停止时移除本工具的组。发现已有 P2P 组时拒绝接管。 |
| Windows `client/direct` | 独立 Windows SDK C++/WinRT DLL；有设备发现/选择窗口，通过系统 PBC 或 PIN 配对，返回实际本地/远端 IPv4，持有会话到传输结束。 |
| `transport.c` | 从 EXE 同目录加载版本化 C ABI；TCP 绑定实际 Direct 本地地址，负责取消与关闭适配器。没有猜测固定网关。 |
| 音频层 | 复用原 TCP 控制 + UDP 媒体、Opus、增益、监听和虚拟驱动。Android 控制监听只绑定 P2P 地址。 |

Android 使用公开 `WifiP2pManager.createGroup()`，不调用热点/网络共享接口。Windows 扫描时采用官方 connector 示例的 advertiser + association-endpoint watcher 流程，明确关闭 `LegacySettings`。这是 P2P 组连接，不用 SSID/密码方式让普通 Wi-Fi 客户端加入。

当前配对采用 Invitation（手机已建组），优先 Push Button，并支持系统要求的 PIN 显示或输入；手机保留系统确认。PIN 只在配对窗口中显示或输入，不写入运行日志。发现列表可能包含其他支持 P2P 的设备，应按手机显示的系统设备名选择。不会自动连接第一个设备，不会自动删除旧配对，也不关闭防火墙。

Wi-Fi Direct 仍有组所有者和组客户端，并非无中心网状网络。网卡、驱动、手机系统以及与现有 Wi-Fi 并发的能力需实测；不能保证同时保持路由器上网或延迟一定优于普通 Wi-Fi。没有合适无线网卡的台式机可继续使用 Wi-Fi（有线接入同一局域网）、USB 或蓝牙。

## 构建与 CI

主程序沿用 Linux / WSL2 MinGW；Direct DLL 在 Windows 使用 Visual Studio C++ 和 Windows SDK 构建，见[构建说明](BUILD.md#wi-fi-direct-原生适配模块)。不引入音频/驱动依赖，也不强制整个客户端改用 Windows SDK。

[本轮 CI 40860949914](https://github.com/1-1-2/MicOpen/actions/runs/40860949914) 的 10 个作业均已通过，构建源码为 `2fcee94`：两端构建、Android 单元与协议测试、debug/release 模拟器回归、Windows 协议/驱动/监听/ADB 回归，以及直连适配器 ABI 2、参数边界、预取消、重复调用、窗口取消/关闭/超时和深浅色只读控件检查。Windows 地址栏的实际渲染像素已核对；日志面板通过真实鼠标事件与剪贴板验证独立滚动、暂停期间缓冲覆写、清除与继续记录。Android 日志缓冲的清除、版本失效、保留界限和并发测试已通过；20 秒自动跟随与实体配对仍需作者复测。

首轮实机反馈与修正见下节；新包仍待作者复测。云端 runner 没有这对实体无线设备，**CI 成功不等于状态 19 已在实体设备上消失，也不等于实体无线配对或音频已验收**。

## 实机检查清单

- Wi-Fi 原路由器连接、手机热点连接均可用，界面只有一个 Wi-Fi 入口。
- 手机 Direct 启动后，电脑能发现并选择它，首次配对确认后有音频；未开启系统热点、未输入 IP。
- 停止后再次连接；电脑取消搜索/配对；退出 Windows 客户端后直连会话释放。
- 手机关闭 Wi-Fi 或停止服务，电脑及时报告断线；再次开启后可重新建立连接。
- 拒绝权限、不支持 P2P、已有其他 P2P 会话时有明确说明；USB、蓝牙不受影响。
- 如需同时上网，分别记录电脑 Wi-Fi / 网线、手机 Wi-Fi / 移动数据的实际结果，不以一组设备推断通用兼容性。

反馈时附手机型号/Android 版本、Windows 版本/无线网卡型号，以及两端错误文字和运行日志即可。手机连接页最底部的「运行日志」可长按节选，或点击「全部复制」；选取时可暂停显示更新，后台继续记录。日志只在当前进程内保留最近 300 条，并受 64 Ki 字符总量限制，退出进程后不保留；暂停时全部复制对应当前显示的快照。测试包来自当前分支 CI，正式版仍保持 v0.1.2。

## 官方参考

- [Android：Create P2P connections with Wi-Fi Direct](https://developer.android.com/develop/connectivity/wifi/wifi-direct)
- [Microsoft：Wi-Fi Direct desktop/API guidance](https://learn.microsoft.com/en-us/windows-hardware/drivers/partnerapps/wi-fi-direct)
- [Microsoft：Wi-Fi Direct sample](https://learn.microsoft.com/en-us/samples/microsoft/windows-universal-samples/wifidirect/)，参考其 `Scenario2_Connector` 和 `ConnectionSettingsPanel` 的公开 API 流程；本模块独立实现 Win32 窗口和生命周期管理，不依赖 UWP 页面。

不使用已弃用的 `Windows.Devices.WiFiDirect.Services` 服务发现接口。

## 2026-10-29 实机反馈后的修正（待复测）

首轮实测出现 Windows 配对状态 `19`，以及手机打开 WLAN 直连设置页后才可被发现。`19` 对应 `DevicePairingResultStatus.Failed`，含义是未知失败；`0x80004005` 是本工具包装的通用失败码，不能据此判定用户没有确认、旧配对冲突或网卡不支持。

- Android 创建组后，Android 13+ 调用公开的 `startListening()`；失败或旧系统尝试 `discoverPeers()`，监听发现状态，避免在已知配对中频繁重启发现。
- 仅在系统回调确认监听/发现请求成功、且未进入配对或连接时显示动态等待指示；这表示应用正在等待，不保证电脑已实际发现手机。失败时显示真实原因与设置入口。
- 绑定组接口的实际 IPv4；不能把新系统可能匿名化的组所有者地址直接当成本机监听地址。
- Windows 把选择设备和建立会话分开：选择「无线直连」立即搜索、点选名称、再点主界面「连接」。设备栏只读并沿用当前主题；搜索窗口支持取消、关闭和超时。
- 配对参数改为 Invitation，并通过官方 `GetDevicePairingKinds()` 对应 Push Button / Display PIN / Provide PIN。失败信息保留状态码和所请求的方式（`-1` 表示未收到配对方式回调），不自动删除配对或静默反复重试。
- Android 的「打开对应设置」位于「准备」标题同行，运行期间保持可用。Wi-Fi → WLAN 设置，蓝牙 → 蓝牙设置，USB → 开发者选项。直连打开公开的 WLAN 设置并提示查找里面的 WLAN 直连入口；没有依赖未经公共 SDK 保证的系统组件名或 settings action。普通 Wi-Fi 浮动面板不是通用的直连设备列表。

这些修改针对代码中已经确认的缺项，状态 19 的具体设备原因仍需新包实测，不能把 CI 成功当成无线配对成功。下一轮优先确认：手机留在 MicLink 内能否被发现；手机是否弹出确认或 PIN；Windows 的状态及配对方式。

参考：[Android 发现与监听 API](https://developer.android.com/reference/android/net/wifi/p2p/WifiP2pManager#startListening(android.net.wifi.p2p.WifiP2pManager.Channel,%20android.net.wifi.p2p.WifiP2pManager.ActionListener))、[微软官方配对示例](https://github.com/microsoft/Windows-universal-samples/blob/main/Samples/WiFiDirect/cs/ConnectionSettingsPanel.xaml.cs)、[配对状态](https://learn.microsoft.com/en-us/uwp/api/windows.devices.enumeration.devicepairingresultstatus)、[配对过程](https://learn.microsoft.com/en-us/uwp/api/windows.devices.wifidirect.wifidirectpairingprocedure)。

### 第二轮反馈：已触发配对，仍返回 19

作者反馈 Windows 在约 12 秒后返回 `状态=19；过程=Invitation，方式=1`；手机有 `discovery active=true`、`peers=1 invited=false`。`方式=1` 是 `ConfirmOnly`，证明 Windows 配对回调已触发；`peers=1` 只表示有一个邻居，`invited=false` 不等于确认窗口已出现，也不等于入组成功。虚拟麦克风、Opus 和 UDP 就绪信息属于本机初始化，尚未证明无线链路或音频握手成功。配对窗口随失败结束而关闭，不据此判断程序崩溃。

这轮保持官方配对参数，补充「确认已提交」与配对耗时；回调提交异常不再静默吞掉。手机日志分别记录每个邻居的状态、WPS 能力与实际组客户端数量，不记录 MAC、配对 PIN 或密码。可见性文案改为「已请求可发现」，区分系统接受请求和电脑实际搜到设备。状态 19 的根因与实体连通仍待确认，本轮没有宣称已修复实体配对。

两端日志新增「清除日志」。Windows 新增「暂停更新／继续更新」、日志正文独立滚轮和可拖动滚动条，工具栏直接衔接标题与正文。暂停只固定显示快照，后台继续记录；暂停时复制全部对应此快照。清除同时清空后台与暂停快照，保留暂停状态，后续仍可记录。Windows 保留最近 200 条，手机保留最近 300 条及 64 Ki 字符；均为当前进程内日志。

补充反馈：手机未出现系统确认，在系统 WLAN 直连页尝试加入电脑也失败，Windows 偶发 `0x80070002`。该 HRESULT 不能独自证明安装目录缺文件；新日志标出「解析所选设备标识／系统配对／建立直连会话」等失败阶段，设备记录为空时明确拒绝并提示重新选择。当前流程仍由手机建组、电脑主动加入，未实现手机系统页主动连接电脑的反向接入。

鼠标位于展开的 Windows 日志面板（含标题和工具栏）时滚轮只滚日志，面板外滚右栏。Android 默认跟随最新日志；手动翻阅后暂停跟随，20 秒未滚动时回到最新，暂停更新期间不自动跳转。
