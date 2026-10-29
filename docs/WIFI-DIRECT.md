# 无线直连 · 开发与实机验收

当前分支新增真正的 **Wi-Fi Direct**，尚未完成用户实机验收，**不发布新 Release**。既有 Wi-Fi、手机热点、蓝牙、USB 的实机反馈不作为新 Direct 的验收证据。

## 怎么连接

1. 手机保持 Wi-Fi 开启，选择「无线直连」，点击「启动」，授予附近 Wi-Fi 设备权限。Android 12L 及更早版本需要定位权限；系统提示时检查定位开关。不用开启手机热点。
2. 等手机显示直连已就绪及本机设备名。Windows 选择「无线直连」，点击「连接」，在搜索窗口中选择这个手机。
3. 首次连接按两端系统提示确认配对。控制端口仍应一致，默认 8125；不用填写 IP。电脑随后沿用现有音频协议连接。
4. 取消搜索或断线后，需要再次点击「连接」选择手机；不会通过自动重连反复弹出搜索窗口。更换手机连接方式前先停止。

路由器和手机热点统一在「Wi-Fi」项下，仍填写实际手机 IP。`--transport hotspot` 保留为 `wifi` 的兼容别名，不会触发 Direct。旧 Android 的 `HOTSPOT` 设置也迁移到普通 Wi-Fi。

## 实现与边界

| 部分 | 责任 |
|---|---|
| Android `WifiDirectTransport` | 检查硬件与 Wi-Fi、创建并拥有 P2P 组、观察状态、交还实际组所有者地址、停止时移除本工具的组。发现已有 P2P 组时拒绝接管。 |
| Windows `client/direct` | 独立 Windows SDK C++/WinRT DLL；有设备发现/选择窗口，通过系统 PBC 配对，返回实际本地/远端 IPv4，持有会话到传输结束。 |
| `transport.c` | 从 EXE 同目录加载版本化 C ABI；TCP 绑定实际 Direct 本地地址，负责取消与关闭适配器。没有猜测固定网关。 |
| 音频层 | 复用原 TCP 控制 + UDP 媒体、Opus、增益、监听和虚拟驱动。Android 控制监听只绑定 P2P 地址。 |

Android 使用公开 `WifiP2pManager.createGroup()`，不调用热点/网络共享接口。Windows 扫描时采用官方 connector 示例的 advertiser + association-endpoint watcher 流程，明确关闭 `LegacySettings`。这是 P2P 组连接，不用 SSID/密码方式让普通 Wi-Fi 客户端加入。

当前配对优先采用 Push Button / ConfirmOnly，手机保留系统确认。发现列表可能包含其他支持 P2P 的设备，应按手机显示的系统设备名选择。不会自动连接第一个设备，不会自动删除旧配对，也不关闭防火墙。

Wi-Fi Direct 仍有组所有者和组客户端，并非无中心网状网络。网卡、驱动、手机系统以及与现有 Wi-Fi 并发的能力需实测；不能保证同时保持路由器上网或延迟一定优于普通 Wi-Fi。没有合适无线网卡的台式机可继续使用 Wi-Fi（有线接入同一局域网）、USB 或蓝牙。

## 构建与 CI

主程序沿用 Linux / WSL2 MinGW；Direct DLL 在 Windows 使用 Visual Studio C++ 和 Windows SDK 构建，见[构建说明](BUILD.md#wi-fi-direct-原生适配模块)。不引入音频/驱动依赖，也不强制整个客户端改用 Windows SDK。

[本次 CI 40797389403](https://github.com/1-1-2/MicOpen/actions/runs/40797389403) 已全部通过，业务源码为 `b9379a8`：两端构建、协议与运行回归、适配器 C ABI/参数边界/预取消/重复调用，以及原生搜索窗口取消、关闭和超时回收。迁移测试覆盖旧热点设置不被误解释为 Direct。云端 runner 没有这对实体无线设备，**编译/模拟器/ABI 通过不等于实机无线配对成功**。

## 实机检查清单

- Wi-Fi 原路由器连接、手机热点连接均可用，界面只有一个 Wi-Fi 入口。
- 手机 Direct 启动后，电脑能发现并选择它，首次配对确认后有音频；未开启系统热点、未输入 IP。
- 停止后再次连接；电脑取消搜索/配对；退出 Windows 客户端后直连会话释放。
- 手机关闭 Wi-Fi 或停止服务，电脑及时报告断线；再次开启后可重新建立连接。
- 拒绝权限、不支持 P2P、已有其他 P2P 会话时有明确说明；USB、蓝牙不受影响。
- 如需同时上网，分别记录电脑 Wi-Fi / 网线、手机 Wi-Fi / 移动数据的实际结果，不以一组设备推断通用兼容性。

反馈时附手机型号/Android 版本、Windows 版本/无线网卡型号，以及两端错误文字和 Windows 运行日志即可。测试包来自当前分支 CI，正式版仍保持 v0.1.2。

## 官方参考

- [Android：Create P2P connections with Wi-Fi Direct](https://developer.android.com/develop/connectivity/wifi/wifi-direct)
- [Microsoft：Wi-Fi Direct desktop/API guidance](https://learn.microsoft.com/en-us/windows-hardware/drivers/partnerapps/wi-fi-direct)
- [Microsoft：Wi-Fi Direct sample](https://learn.microsoft.com/en-us/samples/microsoft/windows-universal-samples/wifidirect/)，参考其 `Scenario2_Connector` 和 `ConnectionSettingsPanel` 的公开 API 流程；本模块独立实现 Win32 窗口和生命周期管理，不依赖 UWP 页面。

不使用已弃用的 `Windows.Devices.WiFiDirect.Services` 服务发现接口。
