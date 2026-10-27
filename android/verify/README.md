# Android 验证入口

| 层级 | 入口 | 能验证的内容 |
|---|---|---|
| JVM 协议模块 | `cd android && ./gradlew :core:test` | 帧、包化、会话与看门狗行为 |
| JNI 接线 | `bash android/verify/jni_smoketest/run.sh` | JNI 符号、参数及错误路径；使用替身 Opus |
| 工具离线自测 | `python3 android/verify/integration/pc_client.py --self-test` | 包解析与断言工具 |
| 真实 app 闭环 | [联调工具](integration/README.md) | TCP 会话、UDP 媒体、停止和重连 |
| APK 与界面 | `ci.yml`（verify-android） | 构建、模拟器与 UI 控件 |

命令以仓库根目录为起点。JNI 替身测试不能证明真实编码质量；模拟器不能替代物理手机的麦克风、系统音效和后台保活验证。已执行结果见[验证报告](../../docs/verification/windows-e2e-report.md)。
