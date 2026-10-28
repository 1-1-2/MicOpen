# 工程文档地图

[← 项目首页](../README.md) · [English overview](../README.en.md)

先按想完成的事找入口，再往实现细节走。下列文档以中文为主；连接适配指南为英文。当前功能、发布包与实机验证的范围可能不同，遇到差异先查交付记录。

## 使用与排查

| 想做的事 | 入口与内容 |
|---|---|
| 第一次连上手机 | [首页快速上手](../README.md#quick-start)：Wi-Fi 四步连接。 |
| 换热点、USB 或蓝牙 | [连接指南](TRANSPORTS.md)：准备顺序、ADB 放置与授权、蓝牙配对、协议边界。 |
| 调整手机采集 | [Android 采集端](../android/README.md)：应用模块、采集与验证入口。 |
| 管理电脑接收与驱动 | [Windows 接收端](../client/README.md)：CLI、外置驱动管理与无声排查。 |
| 试听与抑制监听啸叫 | [监听实验室](../client/monitor/README.md)：设备、独立音量、Ring Out 与实验性 AEC。 |
| 下载最新版本 | [发布记录](RELEASES.md)：产物、实机确认与验证边界。 |
| 看最新交付和已知问题 | [2026-10-28 收尾记录](CLOSEOUT-2026-10-28.md)：实机反馈与蓝牙状态显示缺口；最新产物见[发布记录](RELEASES.md)。 |

## 构建与理解实现

| 想做的事 | 入口与内容 |
|---|---|
| 从源码跑通两端 | [逐步指南](STEP-BY-STEP.md)：检出、构建、安装、连接与分层验证。 |
| 准备工具链或签名 | [构建说明](BUILD.md)：SDK / NDK、Linux 交叉编译、Windows 构建限制、R8 与分发签名。 |
| 理解控制与媒体 | [通信接口](protocol/womic-wifi-protocol-v1.md)：Wi-Fi 控制帧与 UDP 格式；[连接适配](TRANSPORTS.md)：USB / 蓝牙流式扩展。 |
| 理解驱动接入 | [驱动白皮书](protocol/driver-whitepaper.md)：2022 驱动基线、PCM 写入与验证限制，非官方接口承诺。 |
| 了解依赖与分发要求 | [第三方说明](../NOTICE.md) · [项目许可](../LICENSE) · [监听组合许可](../client/monitor/TEST-LICENSE.md)。 |

## 验证与 CI 现场

| 想做的事 | 入口与内容 |
|---|---|
| 判断现有证据能说明什么 | [Windows 验证报告](verification/windows-e2e-report.md) · [连接适配验证记录](TRANSPORTS.md#verification-record-2026-10-28)。 |
| 在真实设备上补验 | [人工互操作清单](verification/android-interop-checklist.md)：记录版本、音质、恢复与长时间运行。 |
| 从本地跑 Android 检查 | [验证入口](../android/verify/README.md) · [协议闭环工具](../android/verify/integration/README.md)。 |
| 触发构建、复核或远程调试 | [CI / Lab 使用说明](../.github/workflows/README.md)：网页、API、产物、暂停点与退出。 |
| 排查远程环境和媒体网络 | [Windows 交互调试](maintenance/windows-interactive-debug.md) · [双机网络通道](maintenance/tailscale-channel.md)。 |
| 维护自动化本身 | [共享 Actions 组件](../.github/actions/README.md) · [CI 脚本](../.github/scripts/README.md)。 |

CI 中本项目两端的闭环通过，不等于与官方 WO Mic 客户端互操作通过，也不替代小时级稳定性和所有手机音效的实测。标准用户交互会话录音 `UNSUPPORTED` 不计作通过；后续用户设备反馈见[收尾记录](CLOSEOUT-2026-10-28.md)。

## 贡献与仓库维护

- **准备提交**：[Git 规范](maintenance/git-policy.md)说明提交身份、说明格式与历史维护约定。
- **配置自动化凭据**：[凭据管理](maintenance/CREDENTIALS.md)只列变量名与存储边界，不包含实际值。
- **受限环境推送**：[工具入口](../tools/README.md)与 [Git Data API 说明](maintenance/github-api-push.md)提供普通 Git 不可用时的后备路线。

问题反馈请附两端版本、连接方式、系统与相关日志。源码、问题反馈、CI 与正式发布统一在 `1-1-2/MicOpen`。
