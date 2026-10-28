# 当前版本自动验证记录

本页以 **MicOpen 本仓库可访问的新运行**为依据。迁移前运行和私人历史资料不作为当前版本的验收入口，也不把已失效的运行链接改写成另一个仓库的链接。

## 2026-10-29 · 本仓库完整 CI

[CI 40735612451](https://github.com/1-1-2/MicOpen/actions/runs/40735612451) 全部成功，对应业务代码提交 `edc3956450446b6eae97253b4832cf8147acab6d`。后续发布文档更新不改变该次验证的应用代码。

| 范围 | 结果与证据 |
|---|---|
| 基础检查、协议测试 | 通过。 |
| Android debug / R8 release 构建 | 通过，包含 app 单测、三 ABI、非调试 release 与压缩信息检查。 |
| 两种 APK 的模拟器验证 | 均通过，包含界面、传统协议及真实 ADB 转发的流式会话、停止与重连。 |
| Windows x64 构建 | 通过，含体积、导入表与资源检查；公开安装器无内嵌第三方驱动。 |
| ADB 生命周期 | Windows 原生六项通过：正常清理、连接失败、取消、直接退出、强制结束，以及保留预先存在的共享服务。测试使用不接触 USB 的 ADB 进程替身，验证的是实际 Windows Job Object 与进程回收。 |
| 流式协议 | 真实客户端的分片、慢帧头、旧能力拒绝、长度越界和提前关闭检查通过。 |
| 监听模块与界面 | DSP、WASAPI worker、监听窗口和主窗口检查通过。 |
| 官方驱动基线 | 9/9 解压与外部目录复制检查通过，安装后系统状态和 status=0 门禁通过。 |
| SYSTEM / 标准用户会话 0 录音 | 通过，录得 288000 B，主峰 1 kHz。 |
| 标准用户交互会话录音（G2） | `UNSUPPORTED`，未计作通过。 |

安装器首次原始回执为 4，随后通过独立系统状态和设备就绪检查；不把原始非零值自动当作权限失败，也不把“INF 已安装”当作音频已可用。

[针对 ADB 修复的先行 Windows 验证 40734552650](https://github.com/1-1-2/MicOpen/actions/runs/40734552650) 同样通过。当前完整 CI 为发布依据；无需依赖迁移前仓库仍然存在。

## 实机反馈与自动化的边界

用户已确认 Wi-Fi、手机热点、蓝牙和有线 ADB / USB 的基本链路可用。自动化新增验证本工具启动的 ADB 进程能随退出回收、共享服务不被误杀；它不替代不同手机、USB 拔插、蓝牙适配器与长时间运行的实机复核。

官方 WO Mic App / Client 互操作、真实采集音质和小时级稳定性没有因本轮 CI 成功而自动变成通过。交互式 Lab、跨 runner 双机音频与 WSS 调试接入未在本轮重跑，所需仓库环境变量也未在本轮配置。

## 复现与报告

从 Actions 选择 `CI`，使用 `task=verify,target=both` 可复核两端；只验证 Windows 可选 `target=windows`。CI 附件包含来源、哈希、选定日志与自动截图，保留期见[工作流说明](../../.github/workflows/README.md)。私人交接包另保留本轮报告和 ADB 测试日志，避免附件过期后失去本地证据。

ADB 生命周期可在 Windows 使用已编译的测试工具运行：

```powershell
python client/tools/test_adb_lifecycle.py client/build/test_adb_lifecycle.exe client/build/test_adb_fixture.exe
```

测试工具由 `bash client/tools/build_adb_tests.sh` 交叉编译；它们不会作为应用或 Google ADB 随 Release 分发。实机逐项记录见[人工检查清单](android-interop-checklist.md)。
