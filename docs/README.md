# 工程文档索引

本目录只放当前 MicLink Open 的使用与工程文档。首次运行从逐步上手指南开始，接口细节由白皮书与通信说明承载。

## 开发与构建

| 文档 | 用途 |
|---|---|
| [`STEP-BY-STEP.md`](STEP-BY-STEP.md) | 从检出源码到 Android/Windows 构建、安装与联调的逐步操作流程 |
| [`BUILD.md`](BUILD.md) | Android 与 Windows 的依赖、构建命令和产物说明 |
| [`../android/README.md`](../android/README.md) | Android 采集端与 Opus 源码准备 |
| [`../client/README.md`](../client/README.md) | Windows 客户端、驱动包选项与本地构建 |
| [`git-policy.md`](maintenance/git-policy.md) | 提交身份、提交说明与历史维护规则 |
| [`CREDENTIALS.md`](maintenance/CREDENTIALS.md) | 凭据变量名、保存位置和泄露处置规则；不含凭据值 |
| [`../tools/README.md`](../tools/README.md) | API 推送工具的安全用法 |

## 协议与驱动

| 文档 | 用途 |
|---|---|
| [`protocol/womic-wifi-protocol-v1.md`](protocol/womic-wifi-protocol-v1.md) | 采集端与接收端的控制、媒体和会话约定 |
| [`driver-whitepaper.md`](protocol/driver-whitepaper.md) | 已有驱动的访问流程、版本基线与 PCM 写入约束 |

## 验证与运维

| 文档 | 用途 |
|---|---|
| [`windows-e2e-report.md`](verification/windows-e2e-report.md) | Windows CI 安装、注音、录音和 GUI 验证记录 |
| [`android-interop-checklist.md`](verification/android-interop-checklist.md) | 官方 WO Mic App/Client 互操作的待执行人工清单 |
| [`tailscale-channel.md`](maintenance/tailscale-channel.md) | 双机 CI 网络通道的安全配置说明；不记录实际凭据或节点信息 |
| [`github-api-push.md`](maintenance/github-api-push.md) | 受限环境下 Git Data API 推送工具的使用边界 |

## 已知验证边界

双机 CI 的 Android app 与本仓库 Windows 客户端链路已通过；官方 WO Mic Windows Client 互操作、真实手机采集质量和小时级稳定性仍需人工验证。不要把 CI 通过描述成所有真实设备场景都已验证。
