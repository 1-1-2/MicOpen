# MicLink Open

为已安装 **WO Mic 虚拟麦克风驱动**的 Windows 电脑，提供 Android 音频采集端和 Windows 接收端，让手机麦克风可用于会议、录音和直播。

Android 端提供音源、输入设备及系统音效控制；Windows 端提供连接管理、输入增益、本地监听和自动重连。项目独立维护，WO Mic 是所兼容的第三方驱动及产品名称。

## 界面

| Windows 接收端 | Android 采集端 |
|---|---|
| ![Windows 客户端](site/assets/windows-client-dark.png) | <img src="site/assets/android-connect-test.png" alt="Android 连接界面" width="240"> |

更多界面见[离线产品介绍](site/index.html)。Android 截图来自模拟器，地址为测试数据。

## 使用前提

- Android 8.0 或更新系统，授予麦克风权限。
- Windows 电脑已从 [WO Mic 官方渠道](https://wolicheng.com/womic/)安装兼容驱动；本项目已验证的驱动基线和限制见[驱动接口白皮书](docs/protocol/driver-whitepaper.md)。
- Wi-Fi / 手机热点：手机与电脑处于可互相访问的网络。USB 需要 ADB 与调试授权；蓝牙需要系统配对并在手机启用蓝牙传输。
- 新增连接适配及验证边界见[连接方式说明](docs/TRANSPORTS.md)；USB、蓝牙的物理设备链路尚待实机验收。

## 从源码构建

先准备 JDK、Android SDK/NDK，以及 Windows 客户端所需的 MinGW-w64 和 Python。按下面的入口分别构建两端：

```bash
# 仓库根目录：准备 Android 的 Opus 源码
bash .github/scripts/fetch-opus.sh

# Android debug 与 R8 release（分发签名见 docs/BUILD.md）
(cd android && ./gradlew :app:assembleDebug :app:assembleRelease)

# Linux 上交叉编译 Windows 客户端
(cd client && ./build.sh)
```

Windows 原生构建运行 `client\build.bat`。构建输出分别位于 `android/app/build/outputs/apk/` 和 `client/build/`。

第一次操作请按[逐步上手指南](docs/STEP-BY-STEP.md)完成检出、环境准备、构建与安装；版本及可选依赖见[构建说明](docs/BUILD.md)。

## 开始使用

1. 在手机打开 MicLink Open，先选择 Wi-Fi、热点直连、USB 或蓝牙，按页面提示完成准备，再点击“启动”等待电脑。
2. 在 Windows 客户端选择相同连接方式；Wi-Fi / 热点填写手机 IP，USB 自动选择或填写序列号，蓝牙选择已配对手机，然后点击“连接”。
3. 在会议软件、录音机或 OBS 的输入设备中选择 WO Mic 虚拟麦克风。

连接异常时，先检查手机服务状态、地址与防火墙；详细排查见[客户端说明](client/README.md)。

## 验证与文档

Android 模拟器闭环、Windows 驱动与录音验证、Tailscale 双机推流已通过 CI。真实手机音质、官方客户端互操作和小时级稳定性仍待人工验证；Windows 标准用户交互会话录音标记为 `UNSUPPORTED`，未计作通过。

- [验证结果与边界](docs/verification/windows-e2e-report.md)
- [工程文档索引](docs/README.md)
- [通信接口说明](docs/protocol/womic-wifi-protocol-v1.md) · [驱动接口白皮书](docs/protocol/driver-whitepaper.md)

## 许可

自有代码以 [MIT 许可](LICENSE)为主；Ring Out 适配及包含它的测试组合使用 GPL-3.0-or-later。第三方组件及驱动说明见 [NOTICE](NOTICE.md)。

构建、自动验证及临时调试环境的操作见[工作流使用说明](.github/workflows/README.md)。
