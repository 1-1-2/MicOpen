# 无线直连 · 手机选择电脑

实验分支 `feat/wifi-direct-phone-initiated`，**尚待实体设备验收，不创建 Release**。旧的手机建组实现保留在 `feat/wifi-direct`，本分支不同时提供两个方向的切换开关。

## 操作

1. 电脑开启 Wi-Fi，在 MicLink 选择「无线直连」，点击「等待连接」。等待窗口显示本机名称；名称以系统提供给手机的列表为准。
2. 手机选择「无线直连」，点击「启动」。在连接页的「选择电脑」列表点选电脑；无结果时点「重新搜索」。Android 13+ 需附近 Wi-Fi 权限，旧系统还需定位权限及定位开关。
3. 电脑核对请求中的手机名称，点击「允许连接」。拒绝后继续等待其他请求；停止等待或关闭窗口会结束此轮。若系统需要 PIN 或确认，按两端实际提示操作。
4. 手机加入电脑的组、取得自己的 IPv4 后，原有控制与音频会话自动继续。两端控制端口仍需一致，默认 8125；不用输入 IP，不开启普通热点。

列表是系统发现的附近 P2P 设备，可能包含其他软件或设备；目前没有宣称已经按 MicLink 服务过滤。请按电脑等待窗口与系统名称核对选择。

## 角色与边界

| 层 | Windows | Android |
|---|---|---|
| Wi-Fi Direct | 发布自主组，监听请求并确认手机 | 搜索设备，用户选中后主动连接，倾向成为组客户端 |
| 地址 | 等待 Connected 与真实 IPv4 端点 | 读取实际组接口的本机 IPv4，等待 DHCP；不把电脑的组主地址当成本机地址 |
| 既有音频协议 | TCP 控制客户端、UDP 音频接收 | TCP 8125 服务端、Opus 采集与 UDP 发送 |

Windows 模块仍是独立 C++/WinRT DLL，监听 ABI 为 **3**，只负责直连、用户确认、端点与生命周期；不引入音频依赖。旧 ABI 2 DLL 与新主程序不能混用，必须整包替换。普通 Wi-Fi、USB、蓝牙、日志与主题沿用验证过的公共代码。

Android 不再在启动时调用 `createGroup()`，不会接管已有直连组。Stop 撤销自己的搜索和连接请求，只移除能归属当前选择的组；若系统在取消后才完成入组、同时隐藏了组主身份，保守保留无法确认归属的网络，必要时在系统设置中断开。不会清空系统配对或重置全局 Wi-Fi。

电脑拒绝手机后保留等待，关闭/取消则停止广播并释放待决请求。只允许一个手机会话，额外请求不自动接入。Windows 取得端点后，仅针对控制服务尚未启动的连接拒绝做最多约 5 秒等待；音频协议不变。

## 分支起点与后续整合

见[分支决策](development/DIRECT-BRANCHES.md)。起点为已验证的 `221f54c`，旧方向的角色相关实现被替换，日志、设置、只读主题、映射文件上限和 CI 修复保留。没有重写或强推旧分支历史。旧方案的问题与实机记录见[旧分支原始记录](https://github.com/1-1-2/MicOpen/blob/221f54c/docs/WIFI-DIRECT.md)。

未来如同时提供两种方向，应把组角色与发现发起方、音频服务器角色分开协调；单个会话只能确定一种建组方向。本分支不预先决定双模式 UI，也不让双方自动同时建组。

## 验证与参考

应用提交 `0bb8491` 已通过 [CI 40874849894](https://github.com/1-1-2/MicOpen/actions/runs/40874849894)，10 个任务全部成功：包括双端构建、原生等待窗口的取消／关闭／允许／拒绝、Android 组归属保护单元测试、既有协议、Windows 回归及 debug／R8 release 的应用验证。命令行首次启动也支持无 IP 的 `--transport direct --connect`。

云端检查无法证明无线网卡与两台手机之间的实体发现、配对和音频可用；这些仍待作者验收。旧分支的 CI 成功不作为新方向的验证结论。

- [Android P2P 搜索与连接](https://developer.android.com/develop/connectivity/wifi/wifi-direct)
- [Microsoft Wi-Fi Direct advertiser 示例](https://github.com/microsoft/Windows-universal-samples/blob/main/Samples/WiFiDirect/cs/Scenario1_Advertiser.xaml.cs)
- [Windows 接收入站请求](https://learn.microsoft.com/en-us/uwp/api/windows.devices.wifidirect.wifidirectconnectionlistener)

本实现使用公开 API，不使用 Wi-Fi Direct Services 的弃用接口、隐藏设置组件或普通 SoftAP。

## 21:07 即刻失败反馈与修正

作者本轮反馈：手机发现 1 个设备，点选后约 69 ms 返回 Android `ERROR=0`；没有系统接受 connect 的日志，尚未确认入组。发现记录 `groupOwners=0` 只是这一刻系统列表的角色标志，不能据此断定电脑没有发布组。

代码中发现一个可明确修正的时序缺陷：在 `connect()` 前显式调用 `stopPeerDiscovery()`。AOSP Android 10/11 的 InactiveState 在这个停止操作里还会执行 `p2pFlush()`，可能使刚选中的底层设备记录失效；系统自己的 connect 流程会在适当阶段停止搜索。现改为直接提交 connect，停止/退出时仍清理自己的搜索。该缺陷与此次日志高度吻合，但还没有手机系统日志来证明厂商 ROM 走到了哪条底层失败分支，不能把它宣称为唯一根因。

`ERROR=0` 是通用内部错误，不能直接当作定位或权限失败。新版记录提交失败的阶段和耗时；Windows 的日志回调扩展记录广播启动、收到请求、用户允许/拒绝、配对条件满足及 IPv4 端点就绪，不记录 PIN。旧版 Windows 没有这些阶段事件，因此旧日志“没有收到请求行”不能证明系统一定没收到请求。独立 DLL 保持监听 ABI 3，新增可选日志导出，不变更音频协议。

本轮应用提交 `f24630d` 已通过 [CI 40880223069](https://github.com/1-1-2/MicOpen/actions/runs/40880223069) 的双端构建、Android 单元测试、原生窗口与日志回调测试。采用 build 任务，没有重跑全套模拟器和 Windows 端到端验收，实体连接仍待作者复测，不创建 Release。参考：[AOSP STOP_DISCOVERY](https://android.googlesource.com/platform/prebuilts/fullsdk/sources/android-30/+/refs/heads/androidx-core-release/com/android/server/wifi/p2p/WifiP2pServiceImpl.java#1838)、[Android discoverPeers / connect](https://developer.android.com/reference/android/net/wifi/p2p/WifiP2pManager)。

## 21:39 两次系统配对失败与搜索提示调整

HUAWEI JSN-AL00a / Android 10 的两轮记录已确认：手机 connect 被系统接受，Windows 的入站请求事件触发，用户在电脑允许。此前连接前停止搜索造成的提交问题已经跨过；现在 Windows 的 PairAsync 分别约 30093 / 30078 ms 返回通用 Failed(19)，未到 FromIdAsync、IPv4 和音频握手。这个状态并非 API 明确返回的 AuthenticationTimeout，不能仅凭耗时断言唯一底层原因。

本轮兼容性调整：Windows 从强制 Invitation 改为 GroupOwnerNegotiation，与手机主动 PBC 连接及[微软示例默认选项](https://github.com/microsoft/Windows-universal-samples/blob/main/Samples/WiFiDirect/shared/ConnectionSettingsPanel.xaml)一致；Windows 的 GO 意向仍为 15，音频角色不变，不自动尝试第二种配对方式，也不绕过系统配对失败。Invitation 并非普遍错误，但不再在这条手机主动发起的实验路径中强制指定它。

失败后的设备信息复核改为沿用请求提供的 Kind，不再把所有 ID 固定当作 AssociationEndpoint；Unknown 使用系统默认重载。日志记录 Kind，并将复核错误显示为十六进制。此前 -2147024894 是 0x80070002，发生在配对失败后的设备记录查询；不能据此认定缺少应用或驱动文件，也不能保证它一定是 Kind 不匹配，失败后记录消失同样可能导致查询失败。

手机搜索动画移到“选择电脑”卡片标题下面，显示“正在搜索电脑，请在下面选择电脑”；正在连接时停止显示搜索动画，改为实际连接进度。准备区不重复展示这段动态状态。

本轮应用提交 `99f31bb` 已通过 [CI 40886645430](https://github.com/1-1-2/MicOpen/actions/runs/40886645430) 的双端构建、Android 单元测试及原生窗口/日志回归。本轮为 build 任务，未重跑全套端到端验证；实体配对仍待作者验证，不创建 Release。参见 [PairingProcedure 的两个过程](https://learn.microsoft.com/en-us/uwp/api/windows.devices.wifidirect.wifidirectpairingprocedure)与 [DeviceInformation 的 ID / Kind](https://learn.microsoft.com/en-us/windows/uwp/devices-sensors/device-information-properties)。

模拟测试与远程 LAB 的条件、能力探针及尚未执行的实验，见[验证环境说明](development/DIRECT-TESTING.md)。
