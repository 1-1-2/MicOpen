# 无线直连诊断开发

分支 `feat/direct-diagnostics` 基于 `2b34e6f`。保留手机选择电脑与既有音频协议；作者实机通过前不发布 Release。

## 双端测试

Windows 点“等待连接”后，先在窗口选择本轮流程，再在手机点选电脑：

| 选择 | 行为 |
| --- | --- |
| A · 显式系统配对 | 原先 Custom.PairAsync，通过保守配对策略后 FromIdAsync |
| B · 直接建立会话 | 用户允许入站请求后直接 FromIdAsync，系统处理必要的配对交互 |

一轮只走一种路径；入站后冻结选择；拒绝返回等待，停止不会自动切换路径。B 不会在 A 失败后自动启用。两种模式均保留用户同意，仍可能出现系统授权。

日志中的 `过程=GroupOwnerNegotiation` 是 A 路径传给 Windows 的配对偏好，不是抓包观察到的空口阶段；`方式=1` 是 Windows [`DevicePairingKinds.ConfirmOnly`](https://learn.microsoft.com/en-us/uwp/api/windows.devices.enumeration.devicepairingkinds)，不是 Wi-Fi P2P 协议错误码。不能仅凭这两个字段判定协商在哪个协议步骤失败。新增日志将 PairAsync、FromIdAsync、系统建组回调与地址就绪分别记录；B 路径不传 A 的显式配对参数。

同一精确版本、同一电脑和手机先各测试一次；有差异再各补至三次。不要同时重置配对、换驱动和改参数。日志带路径、attempt、请求编号、原始结果、相对耗时，不记录 PIN 和密码。

Android target 37 补本地网络权限；API 35+ 记录公开建组事件，旧版保留广播。组形成、IPv4 与应用就绪分开记录。DirectAttempt 由真实 transport 使用，并用虚拟时钟测试迟回调、串行检查、单次交付与 DHCP 期限。地址轮询有界，清理仍要求归属证据。

## 自动化覆盖

- `:app:testDebugUnitTest`：真实 DirectAttempt 与 DirectGroupOwnership 规则。
- Windows `client/direct/test_adapter.py`：原生 A/B 选择、同意/拒绝/取消/超时、颜色与 IPv4 过滤；无无线硬件，不代表系统配对通过。
- 标准 CI `verify/both` 可选 `direct_simulation_api=35` 或 `37`，使用同提交 debug APK，运行双 AVD。
- 独立 `direct-validation.yml` 可复用相同提交的现有构建；共同实现为 `.github/actions/direct-simulation`，不使用 Tailscale。
- `DirectProbeActivity` 仅在 debug 源码集；release 校验要求没有此入口。一个 AVD 用公开 API 建组，另一个复用真实 WifiDirectTransport；组/成员、角色、接口地址必须对应，并绑定组地址完成 TCP 与 10 个有序 UDP echo，再重连一轮。
- ADB 只控制与取证，不承载测试数据。共享虚拟 WLAN 连通不能算 P2P 通过。

环境要求 Emulator 36.5+、可读写 KVM 与支持 P2P 的镜像。预检或测试失败保存 result.json、双端系统与应用日志。双 AVD 不能验证 Windows WinRT/WDI 或 OPPO/Huawei ROM。

最终交付既有分发证书签署的非 debuggable release APK，配合同提交 Windows EXE/DLL、许可及对应源码；不以 CI 代替实体配对和音频验收。

### 只调整测试脚本时复用 APK

标准 CI 可设置 `direct_artifact_run_id` 为已有 Android 构建的 run ID，并设置 `direct_simulation_api=35/37`。此模式不重新构建客户端；必须通过产物 SHA256、原构建成功状态和应用构建输入的 Git diff 三重核对。报告同时保留 APK 源码提交与测试脚本提交；应用源码或构建输入有变化则拒绝复用。

独立 `direct-validation.yml` 在默认分支注册前可能无法用 API dispatch；当前使用已注册的 `ci.yml` 入口，不为运行测试改动稳定 main。

### 2026-10-30 验证进度

- 应用提交 `e1b38a6` 的双端构建、协议及 Windows / Android debug、release 回归通过（CI `41038250075`）；该轮总体失败，原因是旧模拟脚本未识别 Google 系统配对确认窗口，不能称整轮 CI 通过。
- 修正系统窗口识别后，复用同一 APK 的 API 35 双 AVD 两轮建组、地址绑定、TCP 与 UDP 回显及重连通过（CI `41040790473`）。此证据仅覆盖 Android 模拟器 P2P。
- API 37 使用官方 `android-37.0` 镜像；CI `41042860836` 在 APK 安装前失败，原始错误为 `Requested internal only, but not enough space`。尚未运行应用，不能据此判断直连成功或失败。
- 测试环境现显式分配 4 GiB userdata，记录安装前及退出时 `/data` 空间，再复测 API 37。Windows 实机配对仍待作者 A/B 验收，暂不发布 Release。
- CI `41074749196` 的 `/data` 剩余约 3 GiB，存储问题已消除，但安装时系统重启；两端日志均出现 SurfaceFlinger / RegionSampling 的 `hasReadColorBufferDma` 断言。该轮使用托管镜像的 Command-line Tools 12.0，与[上游复现实验](https://github.com/actions/runner-images/issues/14484)报告的旧 avdmanager 问题相符，仍需本仓复测确认。模拟试验现单独固定 Command-line Tools 22.0，并在启动前验证实际生成的 AVD `target`；不改动应用构建工具链，不禁用 SystemUI 或系统配对提示。
- CI `41075701521` 已正确生成 `android-37.0`，两端安装成功并进入真实 P2P 流程；GO 停在 `UserAuthorizingJoinState`。保存的 UI 显示 Android 17 将系统窗口文案改为 `Device connection` / `wants to connect to your device`，旧脚本未点击 Connect，最终探针超时。已为 Wi-Fi 系统窗口增加这组严格识别条件及回归用例，继续复测同一 APK；本轮依然记录为失败。

### 最终自动验证结果

| 验证 | 结果与证据 |
| --- | --- |
| 应用构建与既有回归 | 应用提交 `e1b38a67b4b32567e505bd5df96d1b6447467d6a`；[CI 41038250075](https://github.com/1-1-2/MicOpen/actions/runs/41038250075) 的双端构建、协议、Windows 运行时、Android debug/release 作业通过。该轮总体失败保留，原因见上。 |
| Android 15 双 AVD | [CI 41040790473](https://github.com/1-1-2/MicOpen/actions/runs/41040790473) 成功，同一 APK 完成两轮 P2P、TCP/UDP 回显与重连。 |
| Android 17 双 AVD | [CI 41077109324](https://github.com/1-1-2/MicOpen/actions/runs/41077109324) 成功，测试脚本 `7ca161d`，同一 APK 完成两轮 P2P、TCP/UDP 回显与重连；两台 AVD 元数据均为 `android-37.0`。 |
| 测试脚本回归 | 本地 41 项 Python 检查通过，含 Android 17 实际系统确认窗口与错误 AVD 元数据的回归用例；CI checks 通过。 |
| 实体 Windows / 手机配对 | 尚未验收。交付诊断包，先用同一台 OPPO 各测一次 A/B；不创建 Release。 |

每轮 P2P 验证均确认 GO 为 `192.168.49.1`、GC 为 `192.168.49.207`、组接口 `p2p-wlan0-0`，绑定组地址完成 TCP 与 10 个有序 UDP echo；第二轮重新建组，不能以共享虚拟 WLAN 连通替代此证据。

模拟器使用 Android GO 与 Android GC，不覆盖 Windows WinRT/WDI、实体网卡或厂商 ROM。探针客户端用 TCP 连接 GO 回显服务器；产品手机端的 TCP 控制服务器已有独立协议回归，但尚未与真实 Windows Direct 网络联合验收。这些结果不能证明原先的 Windows 配对问题已经修复。
