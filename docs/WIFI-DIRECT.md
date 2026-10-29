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
