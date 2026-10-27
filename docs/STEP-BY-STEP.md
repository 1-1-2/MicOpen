# 从源码跑通 MicLink Open

本指南按“检出 → 构建 → 安装 → 连接 → 验证”顺序走一遍项目主链路。所有命令都以仓库根目录为起点，示例中的路径均为仓库相对路径。

## 1. 检出源码

```bash
git clone <仓库地址> miclink-open
cd miclink-open
```

项目分为 Android 采集端（`android/`）和 Windows PC 客户端（`client/`）。可以只构建其中一端；连接验证需要两端都可用。

## 2. 构建 Android APK

准备 JDK 17 或更新版本、Android SDK（API 37）、NDK 和 CMake 3.22.1。先获取仓库未内置的 Opus 源码，再运行协议单测和 APK 构建：

```bash
bash .github/scripts/fetch-opus.sh
cd android
./gradlew :core:test
./gradlew :app:assembleDebug
```

APK 默认生成在 `android/app/build/outputs/apk/debug/app-debug.apk`。安装到已开启 USB 调试的手机：

```bash
adb install -r app/build/outputs/apk/debug/app-debug.apk
```

打开 MicLink Open，授予麦克风和通知权限，进入“连接”页并启动服务。调试阶段可以在设置中打开“调试音”，让 app 发送 440 Hz 音调；完成后返回仓库根目录：

```bash
cd ..
```

## 3. 构建 Windows 客户端

### Linux 交叉编译

安装 MinGW-w64、Python 3 和 7-Zip，然后构建：

```bash
sudo apt install gcc-mingw-w64-x86-64 p7zip-full
cd client
./build.sh
cd ..
```

产物在 `client/build/`：`womic.exe` 是客户端，`womic-drvinst.exe` 是驱动安装器。若希望静态链接 Opus 解码器，可按 [`BUILD.md`](BUILD.md) 的说明构建可选的 Opus 静态库；缺少它时客户端仍可构建，但运行时需要可用的 `opus.dll`。

### Windows 原生构建

准备 MinGW-w64（`gcc` 和 `windres` 在 `PATH` 中）及 Python 3，在仓库根目录运行：

```bat
cd client
build.bat
cd ..
```

## 4. 准备并安装官方驱动

公开源码不含 WO Mic 官方驱动二进制。已安装官方驱动的电脑可以跳过本节；否则从官方安装包中准备兼容的驱动目录，目录内应包含 2022 版 `womic.inf`、`womic_1.inf`、`womic_2.inf` 及对应 `.sys`、`.cat` 文件。

以管理员身份打开 PowerShell，在仓库根目录运行：

```powershell
$env:WOMIC_DRIVER_DIR = Read-Host '输入官方驱动目录'
cd client
.\build\womic-drvinst.exe /driver-pack "$env:WOMIC_DRIVER_DIR" /install
cd ..
```

仅从可信的官方来源获取驱动；不要把驱动文件提交到本仓库或随应用重新分发。更多驱动版本和授权边界见 [`NOTICE.md`](../NOTICE.md) 与 [`client/README.md`](../client/README.md)。

## 5. 连接手机与电脑

1. 手机与电脑连到可互相访问的网络。
2. 确认手机 app 已启动服务，状态卡显示“等待 PC 连接”及手机 IP 地址。
3. 在 `client/build/` 启动 `womic.exe`，输入手机 IP 和控制端口（默认 `8125`）后连接。
4. 连接成功后开始推流；Windows 的录音设备列表中应出现 `WOMic`，可在录音机或会议软件中选择。

如果连接失败，检查手机地址、TCP 控制端口 `8125`、Windows 防火墙，以及 app 是否仍在运行。媒体 UDP 端口由客户端在协议协商中提供；双机 CI 的联调配置使用 `8126`。

## 6. 分层验证

先跑不需要设备的离线验证：

```bash
bash .github/scripts/tests/run-tests.sh
python3 android/verify/integration/pc_client.py --self-test
(cd android/verify/integration && python3 test_pc_client.py)
(cd android && ./gradlew :core:test)
```

`pc_client.py` 与已启动的 app 做真实协议闭环时，在仓库根目录运行：

```bash
python3 android/verify/integration/pc_client.py --hold-seconds 12
```

它会验证 TCP 握手、UDP/Opus 包、心跳、停止和重连。完整的模拟器闭环由 `android.yml` 执行；Windows 驱动安装与录音取证由 `windows-e2e.yml` 执行；跨 runner 的真实双机链路由手动触发的 `tailscale-dual.yml` 执行。双机工作流需要仓库配置 `TS_OAUTH_CLIENT_ID` 和 `TS_OAUTH_SECRET` secrets，并要求 Tailscale OAuth 客户端允许 `tag:ci`。

## 7. 输出与清理

脚本默认把截图、诊断和模拟器日志写到仓库根目录 `probe-results/`；Android Gradle 输出在 `android/app/build/`，Windows 产物在 `client/build/`。这些都是本地构建产物，不应提交。脚本临时下载和解析文件使用运行器临时目录或系统提供的临时目录。

构建参数、依赖版本和驱动包的完整说明见 [`BUILD.md`](BUILD.md)；人工互操作限制见 [`android-interop-checklist.md`](verification/android-interop-checklist.md)。
