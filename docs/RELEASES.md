# 发布记录

[项目首页](../README.md) · [正式下载](https://github.com/1-1-2/MicOpen/releases)

## 开发中 · 尚未发布

- 合并 Wi-Fi 与手机热点入口；新增 Wi-Fi Direct 无线直连，电脑搜索手机后配对，无需填写 IP。
- 等待用户实机验收，**确认前不创建新 Release 或版本标签**。开发与测试范围见 [Wi-Fi Direct](WIFI-DIRECT.md)。

后续 GitHub Release Notes 保持简短：概述用户可感知的改动、必要依赖和已知问题；构建、CI 与协议细节链接到对应文档，不在发布说明中重复展开。

## v0.1.2 · 2026-10-29

修复 Windows 退出后遗留本工具所启动 ADB 进程的问题。已有共享 ADB 服务继续复用；本工具新建的服务使用独立端口并纳入 Windows Job Object，正常关闭、失败、取消或强制结束时回收自身进程。不会按进程名误杀其他工具使用的 ADB。

本版本以 [MicOpen CI 40735612451](https://github.com/1-1-2/MicOpen/actions/runs/40735612451) 的新构建和完整验证为依据，业务代码为 `edc3956`。Windows 六项 ADB 生命周期检查、驱动／协议／监听／GUI 检查，以及 Android debug / R8 release 构建与两种模拟器运行均通过；G2 仍为 `UNSUPPORTED`。最新范围见[验证报告](verification/windows-e2e-report.md)，不再依赖迁移前运行作为当前证据。

| 文件 | 内容 |
|---|---|
| `MicOpen-Android-v0.1.2.apk` | 本仓库新构建的 R8 release，以原固定分发密钥重签；签名、三 ABI、内容一致性和 16 KiB 对齐检查通过。 |
| `MicOpen-Windows-x64-v0.1.2.zip` | 含 ADB 清理修复的 Windows 客户端、监听 DLL、驱动管理工具、许可与对应源码。 |
| `MicOpen-v0.1.2-Android-Windows.zip` | 两端合集，内容与独立下载一致。 |
| `SHA256SUMS.txt` | 三个公开下载文件的 SHA-256。 |

USB 仍需外置 Google Platform-Tools；Windows 仍需官方 WO Mic 驱动。升级前若已有旧版遗留的共享 ADB，在确认其他工具未使用时手动结束一次，再测试新版；预先存在的共享服务会保留。

发行标签为 `v0.1.2`；Android 组件代码没有业务变化，其内部版本仍为 `0.1.0` / code `1`。保持原分发证书，可覆盖同签名已安装包。监听处理仅作用于本地副本，SpeexDSP AEC 仍为实验功能。蓝牙监听失效后可能仍显示等待连接的已知状态问题保留于历史收尾记录。

## v0.1.1 · 2026-10-29

> 本节为历史发行记录；其中迁移前 CI 编号不作为当前版本验证依据。

源码、问题反馈、CI 与正式发布统一到 `1-1-2/MicOpen`。默认中文 README，顶部提供完整英文版。

### 本次更新

- 四种连接方式：Wi-Fi、手机热点、配对蓝牙和有线 ADB / USB。手机先选方式并启动等待，Windows 再选相同方式连接。
- 修复蓝牙内部转发的源端口注册顺序，补充握手关闭、超时及接收错误诊断，允许限时内的分片到达。
- 保留 Android 音源、输入设备、系统音效与主题设置，以及 Windows 增益、监听、统计和日志功能。
- USB 已由用户在实体设备上测试确认可用；四种连接方式均有实机可用反馈。ADB 仍需从 Google Platform-Tools 单独准备，不自动下载。

### 产物

| 文件 | 内容 |
|---|---|
| `MicOpen-Android-v0.1.1.apk` | R8 release，使用固定分发签名，含 arm64-v8a / armeabi-v7a / x86_64。 |
| `MicOpen-Windows-x64-v0.1.1.zip` | Windows 主程序、两个监听 DLL、驱动管理工具、许可与对应源码。 |
| `MicOpen-v0.1.1-Android-Windows.zip` | 一次下载 Android 与 Windows 两端，内容与独立下载一致。 |
| `SHA256SUMS.txt` | 上述三个下载文件的 SHA-256。 |

本次沿用已构建并验证的应用二进制，不等待新的 CI。Android 使用已验证且以固定密钥签名的 release；Windows 使用主线完整验证通过的构建。仓库迁移修改涉及文档、链接和维护工具的目标仓库，不改变音频或连接业务代码。Git tag 对应完整源码；Windows 包另附 GPL 组合所需对应源码与上游许可。

GitHub 发行版本为 `v0.1.1`；沿用的 Android 二进制内部版本仍为 `0.1.0` / versionCode `1`。如果已安装包与固定分发签名不同，Android 会拒绝覆盖安装，需先保存设置再卸载旧包；卸载会清除应用数据。

Windows 包不内置 WO Mic 第三方驱动二进制。安装器支持管理自备驱动目录，日常使用请先从 WO Mic 官方渠道安装兼容驱动。监听处理模块作用于本地监听副本，SpeexDSP AEC 仍为实验功能。

### 验证与边界

- 迁移前主线 CI 运行 `40709261485` 全部通过：基础检查、协议测试、Windows 构建与运行验证、Android debug / R8 release 构建及各自模拟器验证。
- Android 分发二进制的构建记录为 `40663752768`；对应签名、三 ABI 与原生库 16 KiB 对齐已有验证记录，分发时保持文件不变。
- 实机反馈确认四种连接方式可用，不扩大为所有手机、蓝牙适配器、驱动版本或小时级稳定性均已覆盖。
- 蓝牙运行中关闭系统蓝牙后，界面可能仍显示等待连接；可停止服务、重新开启蓝牙再启动。详见[收尾记录](CLOSEOUT-2026-10-28.md)。

历史 CI 编号保留用于追溯，不作为当前仓库 Actions 链接。后续运行记录以本仓库 Actions 为准。
