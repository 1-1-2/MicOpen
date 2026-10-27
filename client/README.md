# Windows 接收端

监听增强的设备选择、效果切换和测试步骤见 [监听测试版](monitor/README.md)。

`womic.exe` 接收 Android 端发送的音频，解码后写入电脑上已有的 WO Mic 虚拟麦克风驱动。界面提供输入增益、本地监听、自动重连和会话状态。驱动管理工具 `womic-drvinst.exe` 与应用分别构建。

## 构建

在 Linux 安装 MinGW-w64 和 Python 3，然后从本目录执行：

```bash
./build.sh
./build.sh app
./build.sh drvinst
./build.sh test
```

Windows 原生构建使用 `build.bat`，要求 `gcc`、`windres` 和 Python 在 `PATH` 中。输出位于 `build/`。可选的 Opus 静态库由 `tools/build_opus.sh` 构建；未提供静态库时，运行需要可用的 `opus.dll`。完整依赖见[构建说明](../docs/BUILD.md)。

## 使用

电脑先安装兼容的 WO Mic 驱动。启动 `build/womic.exe`，输入手机 app 显示的地址和端口，连接后在录音或会议软件中选择 WO Mic 输入设备。

命令行也可以直接连接：

```text
womic.exe <手机IP> [端口] [--auto]
womic.exe --help
```

## 驱动管理

应用可以调用同目录的安装器完成驱动状态查询、安装和卸载；应用进程与需要提权的安装操作分开。公开源码不包含第三方驱动文件，干净环境构建时安装器会生成空包。已经安装驱动的电脑可直接使用接收端。

需要管理自备驱动目录时，在管理员终端执行：

```text
womic-drvinst.exe /status
womic-drvinst.exe /driver-pack <官方驱动目录> /install
```

`WOMIC_DRIVER_DIR` 用于指定构建时的驱动目录；`/driver-pack` 用于运行时指定外置目录。应使用同一版本、同一架构配套的 INF、SYS 和 CAT。驱动基线与接口行为见[白皮书](../docs/protocol/driver-whitepaper.md)，第三方说明见[NOTICE](../NOTICE.md)。

## 文件组织

| 路径 | 用途 |
|---|---|
| `src/` | UI、网络、解码和驱动访问源码 |
| `res/` | 图标、manifest、资源脚本 |
| `tools/` | 构建、驱动打包、PE 检查和音频探针 |
| `verify/` | 固定媒体端口等行为验证 |

## 排查

- 连接失败：核对手机服务、地址、端口和防火墙。
- 连接后无声：先看接收包和电平，再检查选中的录音设备与驱动状态。
- 找不到安装器：把两个 EXE 放在同一目录。
- 空包安装器无法直接安装：使用官方安装渠道或配套的外置驱动目录。

当前 CI 结果、G2 交互录音限制及人工验证项见[验证报告](../docs/verification/windows-e2e-report.md)。
