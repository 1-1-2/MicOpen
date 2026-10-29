# 构建说明

本页集中说明工具链和构建选项。首次从源码运行请先按[逐步上手指南](STEP-BY-STEP.md)操作。

## Android

| 组件 | 当前配置 |
|---|---|
| JDK | 完整 JDK 17，需同时有 `java` 和 `javac` |
| Gradle / AGP | 9.3.1 / 9.1.0 |
| Kotlin | 2.2.21 |
| Android API | compileSdk 37.0（显式 minor=0），minSdk 26，targetSdk 37 |
| NDK | CI 使用 28.2.13676358 |
| CMake | 3.22.1 |
| Opus | 构建前获取源码，仓库不内置 |

版本以 `android/gradle/libs.versions.toml`、Gradle wrapper 和模块构建文件为准。通过 `ANDROID_HOME` 或 `ANDROID_SDK_ROOT` 指定本机 SDK，不提交 `local.properties`。新的 SDK 按 `android-37.0` 标识平台，模块已显式指定次版本 0，不依赖某台机器预装的 `android-37` 旧别名。

```bash
# 从仓库根目录执行
bash .github/scripts/fetch-opus.sh
cd android
./gradlew :core:test :app:testDebugUnitTest :app:assembleDebug :app:assembleRelease
```

Windows 可在 Git Bash 中执行 Opus 源码准备脚本，然后在 `android` 中使用 `gradlew.bat`（CMD）或 `.\gradlew.bat`（PowerShell）执行同样的 Gradle 任务。

安装 Android SDK Command-line Tools 后，在其 `bin` 已加入 PATH 的终端执行（与 CI 固定组件一致）：

```bash
sdkmanager --licenses
sdkmanager "platforms;android-37.0" "build-tools;36.0.0" "cmake;3.22.1" "ndk;28.2.13676358"
```

调试 APK 位于 `android/app/build/outputs/apk/debug/`。JNI 构建要求 `android/app/src/main/cpp/opus/` 中存在完整源码；缺失时 CMake 会报错。

## Windows

当前完整客户端采用 **Linux → Windows x64 交叉编译**，与 CI 的构建路线一致。Ubuntu 24.04 可按以下步骤准备；Windows 开发者可在 WSL2 Ubuntu 内复用这些命令，再到 Windows 运行产物。CI 的验证环境是 Ubuntu runner 与 Windows runner，未单独覆盖每种 WSL 配置。Linux 不是客户端运行目标。

```bash
sudo apt-get update
sudo apt-get install -y gcc-mingw-w64-x86-64-posix g++-mingw-w64-x86-64-posix binutils-mingw-w64-x86-64 python3 p7zip-full curl make

# 从仓库根目录执行，明确使用 POSIX 工具链
export CC=x86_64-w64-mingw32-gcc-posix
export CXX=x86_64-w64-mingw32-g++-posix
bash client/tools/build_opus.sh
(cd client && python3 tools/pack_driver.py --empty)
bash client/build.sh
bash client/monitor/build.sh
```

`pack_driver.py --empty` 与公开 CI 一致，明确生成不含第三方驱动的安装器资源。最后一步是可选监听模块；不运行它仍可使用基础监听，但 Ring Out / SpeexDSP 处理不可用。应用和安装器位于 `client/build/`；监听步骤另产出 `miclink-ringout.dll`、`miclink-speex.dll`、许可和对应源码归档。分发包含 Ring Out 的组合时，按[组合许可](../client/monitor/TEST-LICENSE.md)保留相关材料。

`bash client/build.sh app` 只构建应用，`drvinst` 只构建驱动管理工具。`test` 生成驱动包自检程序，需要 Windows 或 Wine 执行；空驱动包会报告跳过，不计作通过。构建脚本检查文件体积、PE 导入表和资源包结构。

### Windows 原生入口的当前限制

仓库保留 `client/build.bat`，但它尚未加入 `monitor.cpp`、`monitor_ui.c` 和 `ui_switch.c` 等当前依赖，也未提供完整的 C++ 编译／链接步骤。因此它不能作为当前完整客户端的可靠构建方法。本轮仅修正文档，不修改构建或应用代码；Windows 上请使用上面的 WSL2 路径，或下载 CI 的交叉构建产物。

### Opus

从 `client/` 执行 `bash tools/build_opus.sh` 可以交叉编译 Opus 静态库，输出到 `deps/opus/`。已有静态库时应用使用静态解码；否则回退到运行时加载 `opus.dll`。编译器通过 `HOST` 指定，临时目录根可通过 `WORK`、`RUNNER_TEMP` 或 `TMPDIR` 设置。

### 自备驱动目录

日常使用以电脑已安装的官方驱动为前提。公开源码不附带驱动二进制；找不到外部目录时，构建脚本生成合法空包，安装操作会明确报告包为空。

`tools/pack_driver.py` 按显式 `--driver-dir`、`WOMIC_DRIVER_DIR` 和仓库候选目录查找驱动。需要构建带外置驱动的测试产物时，从仓库根目录设置：

```bash
export WOMIC_DRIVER_DIR="<本机官方驱动目录>"
(cd client && python3 tools/pack_driver.py --driver-dir "$WOMIC_DRIVER_DIR" && bash build.sh)
```

驱动目录应包含 2022 基线的 `womic.inf`、`womic_1.inf`、`womic_2.inf` 及配套 SYS/CAT。版本、架构和哈希核对由打包工具负责；不要混用不同版本文件。相关第三方说明见[NOTICE](../NOTICE.md)。

## 验证入口

| 入口 | 验证范围 |
|---|---|
| `bash .github/scripts/tests/run-tests.sh` | CI 脚本离线回归 |
| `python3 android/verify/integration/pc_client.py --self-test` | 媒体包解析及协议自测 |
| `python3 android/verify/integration/test_pc_client.py` | 联调工具的监听、重连与二进制通道 |
| `cd android && ./gradlew :core:test :app:testDebugUnitTest` | JVM 协议、Android 设置与传输适配单测 |
| `ci.yml`（Android） | APK、模拟器闭环与界面 |
| `ci.yml`（Windows） | Windows 安装、录音和 GUI |
| `lab.yml`（dual） | 两台 runner 之间的真实音频链路 |

已执行的运行链接和限制见[验证报告](verification/windows-e2e-report.md)。工具链、缓存和系统依赖没有变化时，纯文档修改无需重跑整套远端构建。

具体入口和选项见[工作流使用说明](../.github/workflows/README.md)。

Android debug 与 release 分别输出到 `app/build/outputs/apk/debug/`、`app/build/outputs/apk/release/`（从 Android 模块定位）。release 开启 R8 与资源裁剪，且 `debuggable=false`。未提供签名配置时 release 默认不签名；本机可通过 `MICLINK_ANDROID_KEYSTORE`、`MICLINK_ANDROID_STORE_PASSWORD`、`MICLINK_ANDROID_KEY_ALIAS`、`MICLINK_ANDROID_KEY_PASSWORD` 配置发布签名。CI 显式使用 `-PmiclinkTestSigning=true`，不持有分发私钥；其 release 用于功能验证，分发前在本机签名。调试构建与测试签名均不能代替正式分发身份。

## Wi-Fi Direct 原生适配模块

音频客户端和监听模块仍可在 Linux / WSL2 下交叉编译。新增的 `miclink-direct.dll` 使用系统 C++/WinRT 投影，需在 Windows 安装 Visual Studio 2022（或更新版本）的 C++ 桌面开发工具和 Windows 10/11 SDK，在 PowerShell 执行：

```powershell
./client/direct/build.ps1
python client/direct/test_adapter.py client/build/miclink-direct.dll
```

把 DLL 放在 `womic.exe` 同目录。缺少该 DLL 只影响无线直连；Wi-Fi、USB 和蓝牙沿用原路径。不能把 Linux 交叉编译主程序成功等同于已构建完整 Direct 功能。CI 先在 Windows 构建适配器，再与 Linux 构建的客户端合并成同一来源清单；Lab 共用此流程。物理无线连接不由云端托管 runner 验收。
