# Android 采集端

Android 端负责音频采集、Opus 编码和向 Windows 接收端发送音频，界面使用 Kotlin 与 Compose。当前支持音源/输入设备选择、推流音量、系统音效、调试音和连接状态显示。

## 构建

在仓库根目录准备 Opus，再进入本目录构建：

```bash
bash .github/scripts/fetch-opus.sh
cd android
./gradlew :core:test :app:assembleDebug :app:assembleRelease
```

Windows 上可在准备好 Opus 源码后使用 `gradlew.bat`。工具链版本见[构建说明](../docs/BUILD.md)，完整安装流程见[逐步上手指南](../docs/STEP-BY-STEP.md)。

## 模块

| 路径 | 职责 |
|---|---|
| `app/` | 采集服务、Compose 界面、JNI 与设置 |
| `core/` | 不依赖 Android 的帧编解码、包化及会话控制 |
| `verify/` | JNI 接线检查与协议闭环工具 |
| `gradle/` | 版本目录和 Gradle wrapper 配置 |

## 运行与验证

安装 APK 后授予麦克风权限，启动采集服务，再用本项目 Windows 客户端连接。模拟器可以启用调试音，使自动化测试不依赖物理麦克风。

```bash
python3 android/verify/integration/pc_client.py --self-test
```

上面的命令从仓库根目录执行。真实设备与模拟器闭环见[联调指南](verify/integration/README.md)。通信字段见[接口说明](../docs/protocol/womic-wifi-protocol-v1.md)。本项目客户端之间的验证结果不能替代真实手机采集质量或官方客户端互操作测试。

Android debug 与 release 分别输出到 `app/build/outputs/apk/debug/`、`app/build/outputs/apk/release/`（从 Android 模块定位）。release 开启 R8 与资源裁剪，且 `debuggable=false`。未提供签名配置时 release 默认不签名；本机可通过 `MICLINK_ANDROID_KEYSTORE`、`MICLINK_ANDROID_STORE_PASSWORD`、`MICLINK_ANDROID_KEY_ALIAS`、`MICLINK_ANDROID_KEY_PASSWORD` 配置发布签名。CI 显式使用 `-PmiclinkTestSigning=true`，不持有分发私钥；其 release 用于功能验证，分发前在本机签名。调试构建与测试签名均不能代替正式分发身份。

界面中的“调试音”是无麦克风链路测试功能，与 APK 的 debug/release 构建类型无关。
