# Windows 客户端端到端验证

> 本文汇总 Windows CI 验证结论，最新一次整理后回归为 2026-10-27。工作流和源码持续变化时，以仓库根 README 与当前 workflow 为准。

## 2026-10-27 整理后复核

[Windows 端到端运行 40447139766](https://github.com/1-1-2/WKB_test/actions/runs/40447139766) 的交叉编译与真实 Windows 验证均通过，覆盖驱动安装、SYSTEM/标准用户会话 0 录音、GUI 和输入控件。标准用户交互会话录音仍为 `UNSUPPORTED`，没有计作通过。

同一代码快照还通过了 [Android 构建与模拟器闭环](https://github.com/1-1-2/WKB_test/actions/runs/40447133161) 和 [Tailscale 双机联调](https://github.com/1-1-2/WKB_test/actions/runs/40447145670)。测试快照为 `303267d5461775cf6c702eb163029defc52d3265`，文件树为 `97688eaad8f7b2160cfb878e45e28bbdefd1733b`；后续更新仅涉及文档与截图。双机首轮在 MinGW 安装阶段三次超时，重跑失败作业后成功，未修改产品代码。

## 2026-10-27 工作流改造复核

[全量回归 40491670551](https://github.com/1-1-2/WKB_test/actions/runs/40491670551) 使用共享组件完成 Windows 验证、Android 模拟器闭环及双机音频取证。公开构建包改为强制空驱动包，Windows runner 现场获取并核验官方夹具；原生解压 9/9 与外部目录复制 18/18 均通过。外部目录路径因此覆盖了原先未被内嵌包测试触及的窄／宽字符串格式错误，现已修复并加入回归。

安装器首次回执可能为设备尚未就绪的 4；回执原样保留，后续独立检查驱动文件、注册表及 `--status=0`，再执行实际录音。不能把首个非零回执预先归为 UAC，也不能只看“INF 已安装”就认定设备可用。G2 标准用户交互会话仍为 UNSUPPORTED。

## 覆盖范围

| 场景 | 结果 | 能证明什么 |
|---|---|---|
| Windows 客户端与驱动安装器构建 | 通过 | 目标架构编译、资源打包和导入表检查完成 |
| 官方驱动安装与设备就绪 | 通过 | CI Windows runner 上驱动安装器能安装并检测目标设备 |
| SYSTEM 会话音频探针 | 通过 | `e2e_probe` 录得 288000 B，主频为 1 kHz |
| GUI 冒烟 | 通过 | 窗口可启动，关键控件和状态断言通过 |
| 本项目 Android app ↔ Windows 客户端双机联调 | 通过 | 跨网控制/媒体链路与音频取证见双机 workflow |

对应自动化入口是 [CI Windows 任务](../../.github/workflows/ci.yml)。CI artifact 由 GitHub Actions 按运行编号保存；此报告不保留 runner 的临时路径或账号凭据。

## 结果边界

这些结果来自托管 Windows runner 和模拟器/CI 联调环境，不能证明每种个人电脑、手机 ROM 或驱动状态都一致。以下事项仍待人工验证：

- Android app 与官方 WO Mic Windows Client 互操作；
- 真实 Android 手机上的录音质量、前台服务保活与设备兼容性；
- 小时级连续推流、长时间网络抖动和恢复表现。

Android 人工互操作流程见 [`android-interop-checklist.md`](android-interop-checklist.md)。

## 重跑方式

在 GitHub Actions 页面选择 `CI` workflow 的 Windows 验证任务 并手动运行，或使用已授权的 GitHub CLI。凭据由 CLI/平台凭据管理器处理；不要将 PAT 写入命令参数、脚本或日志。运行后先确认 job 到达 Windows runner，再按失败步骤区分 runner 环境故障与客户端行为。
