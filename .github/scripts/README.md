# CI 脚本

这些脚本供 GitHub Actions 与本地复现使用。仓库资源从脚本位置推导，SDK、输出和临时目录可通过环境变量配置。

| 脚本 | 用途 |
|---|---|
| `setup-android-sdk.sh` | 定位 SDK 和命令行工具 |
| `verify-kvm.sh` | 检查模拟器加速前提 |
| `start-emulator.sh` | 创建/启动 AVD，按时限等待就绪 |
| `fetch-opus.sh` | 准备 Android Opus 源码 |
| `install-app.sh` | 安装 APK 并启动调试采集 |
| `ui-capture.sh` | 五屏截图及控件断言 |
| `collect-diagnostics.sh` | 有时限地收集诊断 |
| `emu-redir.py`、`udp-relay.py` | 双机测试的端口与媒体中继 |
| `womic-hello-probe.py`、`womic-record-probe.py` | 控制探活与 Windows 录音检查 |
| `watch-ci-run.sh`、`debug-help.sh` | 作业观察与本地调试辅助 |

从仓库根目录运行离线测试：

```bash
bash .github/scripts/tests/run-tests.sh
```

`RUNNER_TEMP` 或 `TMPDIR` 可用于临时目录；截图和诊断默认进入仓库根目录 `probe-results/`。各脚本配置以文件头及对应 workflow 为准。网络通道和 SSH 调试见[双机通道](../../docs/maintenance/tailscale-channel.md)与[交互调试](../../docs/maintenance/windows-interactive-debug.md)。
