# 无线直连：模拟测试与远程 LAB 的边界

当前 Lab workflow 的 Windows 端使用 windows-latest，Android 端使用 Ubuntu 上的 Android Emulator；通过 Tailscale/SSH 提供交互控制。Lab 的远程入口并不额外提供无线网卡或把 Windows 和手机放到同一射频环境。本轮先通过普通 CI 只读核查同类 runner 的能力，不启动交互 Lab、不修改凭据。

| 层次 | 可以验证 | 不能据此证明 |
|---|---|---|
| Windows 原生窗口 / 配对状态模拟 | 允许、拒绝、取消、超时、PIN 窗口、日志回调；状态 19 只有满足既定新查询条件才继续，拒绝和取消不能越过 | 实体手机能否完成 WPS / GO 协商 |
| 双 Android AVD，Emulator 36.5+ | 官方文档支持模拟器之间 Wi-Fi Direct，可进一步试验发现、连接回调、组角色、取址、断开重连和手机 UI | 华为 Android 10 ROM 与 Windows 驱动的互通；当前尚未跑过这个双 AVD 实验 |
| 当前云端 Windows + Android Lab | Win32/WinRT 与应用运行、已有 TCP/UDP/Opus 协议、日志与界面；实际无线能力取决于只读探测结果 | 没有原生 802.11 适配器时的真实 Windows Wi-Fi Direct 配对 |
| 有真实设备的远程 Lab | Windows 无线网卡和同地 Android 的发现、配对、DHCP、音频，USB ADB 与远程桌面用于控制和采日志 | 其他手机 ROM / 网卡组合均可用 |

Android 官方在 2026 年提供的新网络栈是重要改进：Emulator 36.5+ 可互相发现和通信。不能继续笼统断言模拟器完全不支持 Direct；但多个 AVD 共享虚拟 Wi-Fi，不等于 Windows 主机自动获得一个可供 WinRT 配对的原生 P2P 适配器。

能力探针只读取网络接口类型、驱动查询结果、模拟器版本与 KVM 权限，不启动/停止系统服务、不发起配对。输出带 pairing_tested=false；缺少硬件记录为不具备该验证条件，不记成配对测试通过。当前 scratch 环境没有可读写 KVM，也未找到 Android Emulator SDK，双 AVD 应在具备 KVM 的 runner 上另行执行。

本轮 CI 探测结果待写回。建议先用探针确认条件，再把 Android 的双 AVD 回归作为独立实验；针对 30 秒 Failed(19) 的 Windows/华为互通，最终仍需真实设备记录，不能用单元测试通过替代。

参考：[Android Emulator 网络](https://developer.android.com/studio/run/emulator-networking)、[互连模拟器](https://developer.android.com/studio/run/emulator-networking-interconnect)、[GitHub hosted runner](https://docs.github.com/en/actions/concepts/runners/github-hosted-runners)。
