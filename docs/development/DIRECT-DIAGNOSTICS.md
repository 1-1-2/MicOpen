# 无线直连诊断开发

分支 `feat/direct-diagnostics` 基于 `2b34e6f`。保留手机选择电脑与既有音频协议；作者实机通过前不发布 Release。

## 双端测试

Windows 点“等待连接”后，先在窗口选择本轮流程，再在手机点选电脑：

| 选择 | 行为 |
| --- | --- |
| A · 显式系统配对 | 原先 Custom.PairAsync，通过保守配对策略后 FromIdAsync |
| B · 直接建立会话 | 用户允许入站请求后直接 FromIdAsync，系统处理必要的配对交互 |

一轮只走一种路径；入站后冻结选择；拒绝返回等待，停止不会自动切换路径。B 不会在 A 失败后自动启用。两种模式均保留用户同意，仍可能出现系统授权。

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
