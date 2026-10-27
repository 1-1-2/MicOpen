# 构建说明

本页集中说明工具链和构建选项。首次从源码运行请先按[逐步上手指南](STEP-BY-STEP.md)操作。

## Android

| 组件 | 当前配置 |
|---|---|
| JDK | 17 或更新版本 |
| Gradle / AGP | 9.3.1 / 9.1.0 |
| Kotlin | 2.2.21 |
| Android API | compileSdk 37，minSdk 26 |
| NDK | CI 使用 28.2.13676358 |
| CMake | 3.22.1 |
| Opus | 构建前获取源码，仓库不内置 |

版本以 `android/gradle/libs.versions.toml`、Gradle wrapper 和模块构建文件为准。通过 `ANDROID_HOME` 或 `ANDROID_SDK_ROOT` 指定本机 SDK，不提交 `local.properties`。

```bash
# 从仓库根目录执行
bash .github/scripts/fetch-opus.sh
cd android
./gradlew :core:test :app:assembleDebug
```

调试 APK 位于 `android/app/build/outputs/apk/debug/`。JNI 构建要求 `android/app/src/main/cpp/opus/` 中存在完整源码；缺失时 CMake 会报错。

## Windows

Linux 交叉编译要求 MinGW-w64、Python 3 和 7-Zip；Windows 原生构建要求 MinGW-w64 的 `gcc`、`windres` 及 Python 在 `PATH` 中。

```bash
cd client
./build.sh app
./build.sh drvinst
./build.sh test
```

`./build.sh` 默认构建应用和安装器。Windows 原生入口为 `client/build.bat`。产物位于 `client/build/`，构建脚本检查文件体积、PE 导入表和资源包结构。

### Opus

从 `client/` 执行 `bash tools/build_opus.sh` 可以交叉编译 Opus 静态库，输出到 `deps/opus/`。已有静态库时应用使用静态解码；否则回退到运行时加载 `opus.dll`。编译器通过 `HOST` 指定，临时目录根可通过 `WORK`、`RUNNER_TEMP` 或 `TMPDIR` 设置。

### 自备驱动目录

日常使用以电脑已安装的官方驱动为前提。公开源码不附带驱动二进制；找不到外部目录时，构建脚本生成合法空包，安装操作会明确报告包为空。

`tools/pack_driver.py` 按显式 `--driver-dir`、`WOMIC_DRIVER_DIR` 和仓库候选目录查找驱动。需要构建带外置驱动的测试产物时，从仓库根目录设置：

```bash
export WOMIC_DRIVER_DIR="<本机官方驱动目录>"
(cd client && ./build.sh)
```

驱动目录应包含 2022 基线的 `womic.inf`、`womic_1.inf`、`womic_2.inf` 及配套 SYS/CAT。版本、架构和哈希核对由打包工具负责；不要混用不同版本文件。相关第三方说明见[NOTICE](../NOTICE.md)。

## 验证入口

| 入口 | 验证范围 |
|---|---|
| `bash .github/scripts/tests/run-tests.sh` | CI 脚本离线回归 |
| `python3 android/verify/integration/pc_client.py --self-test` | 媒体包解析及协议自测 |
| `python3 android/verify/integration/test_pc_client.py` | 联调工具的监听、重连与二进制通道 |
| `cd android && ./gradlew :core:test` | JVM 协议模块 |
| `ci.yml`（Android） | APK、模拟器闭环与界面 |
| `ci.yml`（Windows） | Windows 安装、录音和 GUI |
| `lab.yml`（dual） | 两台 runner 之间的真实音频链路 |

已执行的运行链接和限制见[验证报告](verification/windows-e2e-report.md)。工具链、缓存和系统依赖没有变化时，纯文档修改无需重跑整套远端构建。

具体入口和选项见[工作流使用说明](../.github/workflows/README.md)。
