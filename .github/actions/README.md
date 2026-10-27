# 共用 Actions 组件

这里的目录是供 [CI 与 Lab](../workflows/README.md) 调用的本地 composite actions，不是独立工作流，不会自行被 push 或 dispatch 唤起。`workflows` 负责触发条件、runner、依赖、并行和产物下载；这里负责在当前 runner 上完成一段有明确前提和结果的操作；[scripts](../scripts/README.md) 提供底层工具。

## 组件索引

| 组件 | 平台与调用者 | 前提 | 完成什么、产生什么 |
|---|---|---|---|
| [android-tools](android-tools/action.yml) | Ubuntu；Android 构建或运行组件 | 已 checkout，SDK 可定位/下载 | 设置 Java 17；`purpose=build` 安装编译 SDK/NDK/CMake，`emulator` 安装模拟器组件。不启动模拟器、不生成 APK。 |
| [build-android](build-android/action.yml) | Ubuntu；CI/Lab 的构建 job | 已 checkout，可获取依赖 | 调用 android-tools、取得 Opus、构建全默认 ABI 的 **Debug APK**，经允许清单上传 `build-android-run-attempt`。当前不产出 R8 Release。 |
| [build-windows](build-windows/action.yml) | Ubuntu 交叉构建；CI/Lab | 已 checkout，可安装工具链 | 构建 Windows 应用及无内嵌第三方驱动的安装器，生成来源/哈希清单并上传 `build-windows-run-attempt`。不安装驱动。 |
| [android-runtime](android-runtime/action.yml) | Ubuntu/KVM；CI、Lab、dual-linux | 调用方已下载/核验 APK 到 `apk/` | 调用 android-tools，创建模拟器并安装启动 app；`prepare` 为上层双机准备现场，`installed` 交接安装现场，`verify` 执行 UI/协议验证。验证材料进入 `probe-results/`，由 workflow 挑选上传。 |
| [windows-runtime](windows-runtime/action.yml) | Windows；CI/Lab | 调用方已下载/核验 EXE 到 `dist/` | 现场取得哈希固定的官方驱动夹具、原生解压/复制自检及安装；`installed` 交接，`verify` 再执行录音、权限矩阵与 GUI。不把驱动二进制打入公开产物。 |
| [dual-linux](dual-linux/action.yml) | Ubuntu/KVM；CI/Lab 双机左端 | APK 在 `apk/`；OAuth；对端运行同一次 run/attempt | 调用 android-runtime 的 prepare，加入 tailnet，发现 Windows，建立模拟器 TCP/UDP 转接，统计媒体包并结合对端结果判定。可调用 lab-session 暂停。 |
| [dual-windows](dual-windows/action.yml) | Windows；CI/Lab 双机右端 | EXE 在 `dist/`；OAuth；同轮 Linux 对端 | 加入 tailnet，取得驱动并安装，控制探活、启动客户端，SYSTEM 录音验证测试音。可调用 lab-session 暂停。它保留双机专属阶段，不重复执行整套 windows-runtime 权限矩阵。 |
| [lab-session](lab-session/action.yml) | Linux/Windows；Lab 或 runtime/dual 的暂停点 | 当前 runner 已入 tailnet；有效操作端公钥；`LAB_ROLE` 已设置 | 配置普通 OpenSSH，按需启动 Upterm Relay，发布连接附件并等待；显式结束/到期后终止受管命令并导出会话状态。不会重新入网或重建项目。 |

构建组件自己上传构建包；runtime/dual 由上层 workflow 调用 `ci.py stage` 后上传报告；lab-session 自己上传 readiness、connection、session。具体内容和保留期以[公开产物规则](../workflows/README.md#artifact-的生产与上传)为准。组件不会自动把人工文件或整个工作区上传。

## 参数与状态约定

| 参数 | 使用者与约定 |
|---|---|
| `purpose` | android-tools：`build`（默认）或 `emulator`。 |
| `mode` | android-runtime：`prepare/installed/verify`；windows-runtime：`installed/verify`；默认 verify。 |
| `pause_at` | runtime/dual：暂停真实阶段；不适用组合由 workflow plan 拒绝。网页的 ready 在单端 verify 中转换为内部 before-test。 |
| `minutes` | Lab 就绪后的操作窗口，默认 20；workflow 校验 1–120。不是整份 job 的运行时限。 |
| `stream_logs` | 默认 `'false'`，仅显式 `'true'` 公开受管 SSH 命令/正文。composite 输入按字符串比较。 |
| `ssh_transport` | 组件内部使用 `tailnet/relay`，分别对应网页的 **Tailscale Tailnet / Upterm Relay**；plan 统一转换，组件不各自猜测显示名。只选择操作端 SSH 路径，不切换双机媒体网络。 |
| `public_key` | 操作端的 SSH 公钥，来自 Repository Variable `UPTERM_DEBUG_PUBKEY`；私钥仅留在操作端。该历史变量名同时服务两种接入方式。 |
| `oauth_client_id/oauth_secret` | 仅 dual 组件负责入网时使用，由 workflow 的 Secrets 明确传入；lab-session 不读取 OAuth。 |
| `pause/paired` | dual 的 pause 决定就绪暂停；lab-session 的 paired 同步两端截止时间及收尾。故障留场不强等失败的另一端。 |
| `test_window_seconds` | dual 等待测试对端完成的预算，默认 1200 秒，与人工操作窗口分开。 |

`GITHUB_WORKSPACE` 是本次 checkout 根；`RUNNER_TEMP`、SDK 路径、`LAB_ROOT` 来自运行环境，不固定为某台电脑的绝对路径。lab-session 的调用者需设置角色；单端 `windows/android/linux`，双端 `linux-app/windows-client`。若独立复用组件，还要准备相应 `CONTROL_PORT/MEDIA_PORT/REDIR_PORT`，并按 action 中的 `env` 传入必要的 GitHub API token。

组件在同一个 job 里按步骤执行，安装状态、进程与环境留在同一台机器。另一个 job 的 `needs` 只能安排依赖，不能继承已安装现场；跨 job 只通过明确的 artifact 和 tailnet 通道交换内容。workflow 必须先 checkout 后才能 `uses: ./.github/actions/...`。

## 修改时一起核对

- 组件阶段、脚本入口及硬预算：注释中的旧阶段编号用于追溯，逐步映射见 [migration-map.json](migration-map.json)，不要因编号不连续就删除阶段。
- 修改构建路径：同时核对 `ci.py` 允许清单、workflow 下载位置、运行组件输入，以及 manifest 来源验证。
- 修改暂停/网络：同时核对单端与双端、共享 session、默认日志关闭、失败与到期收尾；Upterm 不提供原生 UDP 通道。
- 修改 verdict：保留 PASS/FAIL/UNSUPPORTED/未执行的区别，尤其 G2 不能因 GUI 或 SSH 成功变成 PASS。

离线脚本回归入口见 [scripts README](../scripts/README.md)；完整操作步骤、两种接入拓扑和已有实测证据见 [workflow README](../workflows/README.md)。
