# 监听测试版

这是 Windows 监听增强测试版。界面采用原生控件，视觉统一留待后续调整。安卓端和虚拟麦克风的格式、协议保持原有行为。

## 开始测试

1. 将测试包完整解压到同一目录，运行 `womic.exe`。两个 `miclink-*.dll` 必须与它放在一起；移动整个目录即可换电脑使用。
2. 按原有流程连接手机。点击主界面「监听」旁的「设置」，打开独立的「监听实验室」。
3. 选择输出设备，然后勾选「启用监听」。默认跟随 Windows 的默认声音输出；选择具体设备后固定使用该设备。
4. 勾选「启用啸叫抑制 / AEC」，选择处理模块。取消勾选即可对比原声；无需停止监听。监听音量独立于虚拟麦克风输出增益。
5. 优先比较 Ring Out 与原声。观察陷波数量并听取语音是否受损。「重置当前算法」只清空选中模块的学习状态，不改变延迟，也不重启播放设备：Ring Out 清空陷波，AEC 重新收敛。
6. 若要比较 AEC，选择 SpeexDSP，调整「播放到返回延迟」。这个值包含播放排队、声学传播、手机采集编码、网络和接收解码等时间，默认 100 ms，可选 10–400 ms。修改后重新收敛；没有一个适合所有设备的固定值。

监听、处理均默认关闭，音量初始为 70%。关闭设置窗口会隐藏窗口，监听保持当前状态；关闭主程序才退出。设备和处理偏好只在本次程序运行中保留，不写入本机绝对路径或跨机器不可用的配置文件。

| 项目 | 行为与边界 |
|---|---|
| 跟随系统默认输出 | 默认设备变化后自动迁移，检测间隔约 1 秒 |
| 手动选择设备 | 点击即请求切换，后台打开新端点；不要求手动关开监听 |
| 指定设备不可用 | 暂停并重试，不自动换到其他扬声器；选项仍保留 |
| 效果切换 | 两个模块持续运行、保留学习状态，以约 20 ms 时间常数混合过渡，约 140 ms 接近稳定；无需重建网络连接 |
| 设备切换 | 新端点准备好后接管，并淡入；设备初始化可能有短暂间隙，蓝牙尤其如此，不能保证完全无缝 |
| 模块缺失 | 窗口明确显示不可用，回到原声，不影响虚拟麦克风和连接 |
| 缓冲积压 | 丢弃过期监听音频，最多保留约 80 ms 待处理音频；不会让监听阻塞收包线程 |
| 与主音频的关系 | 处理使用监听独立副本，不改变虚拟麦克风收到的 PCM；已有静音和本地增益位于分支之前，仍会影响监听 |

## 两个可切换模块

**Ring Out**：采用 Dusk Audio 已有的自动啸叫检测和陷波核心，没有自行重写算法。上游采用频谱峰值、谐波关系、持续时间和增长趋势判断反馈。当前适配连续重新开启上游的 60 秒学习窗口，不做输出增益补偿。陷波可能改变音色；已经学习的陷波保持到重置 Ring Out、设备切换、缓冲积压或会话清理；修改 AEC 延迟只重置 AEC。纯持续音也可能被判为反馈，不能承诺完全不误伤。

**SpeexDSP AEC（实验）**：48 kHz、10 ms 帧、200 ms 自适应滤波尾长。参考取自本监听分支实际提交给播放设备的 PCM，包含监听音量和切换混合。它没有采集其他软件的系统回环声音，也没有自动补偿手机与电脑时钟漂移；渲染失败、积压或会话重置会清理参考与学习状态。这是评估当前反馈路径是否适合 AEC 的实验模块，无法保证它比陷波更好，也不能把合成测试结果当作真实房间效果。两种算法均不增加 AGC。

不提供一个无法实际作用于网络 PCM 的「Windows 系统 AEC」选项。安卓系统 AEC/AGC 也没有改动。未来模块可以复用窄 C ABI；当前只加载两个固定文件名，不执行对任意第三方插件目录的自动扫描。

## 构建和复核

```bash
bash client/tools/build_opus.sh
bash client/build.sh
bash client/monitor/build.sh
```

需要 MinGW x64 的 GCC、G++ POSIX 工具链、Python、curl、make。SpeexDSP 1.2.1 首次构建时下载，校验固定 SHA-256，后续使用 `client/deps/speex`；Ring Out 的固定版本核心已随源码保存。应用不依赖 VC++ Redistributable、独立 C++ 或 pthread DLL。

在 Windows 解压目录中执行：

```powershell
.\test_monitor_dsp.exe "$PWD/miclink-ringout.dll" "$PWD/miclink-speex.dll"
.\test_monitor_host.exe
```

第一项是无硬件的模块加载、持续测试音衰减、参考对齐的 AEC 收敛、输入隔离、重置与反复构造销毁测试。第二项验证真实 Windows worker、设备枚举、指定设备不存在时禁止回退、参数切换和退出。没有播放端点时，播放相关项明确报告 `SKIP`，其余断言仍执行。

上游完整算法测试可在 Linux 运行：

```bash
g++ -std=c++17 -O2 -pthread -Iclient/monitor/vendor/ringout \
  client/monitor/vendor/ringout/RingOutDSP.cpp \
  client/monitor/vendor/ringout/upstream-test.cpp -o /tmp/ringout-test
/tmp/ringout-test
```

完整 CI 还会运行原有驱动、协议、GUI 验证。真实手机、扬声器和房间的测试需另行记录，特别是近距离反馈、语音/音乐误伤、蓝牙切换和长期 AEC 时钟漂移。自动测试不替代这些项目。

## 源码和许可

- 接口：`plugin.h`；适配：`ringout.cpp`、`speex.c`。
- 监听线程与设备：`../src/monitor.cpp`；原生测试窗口：`../src/monitor_ui.c`。
- Ring Out 上游：[dusk-audio/dusk-audio-plugins](https://github.com/dusk-audio/dusk-audio-plugins)，固定提交 `e0991c57cac4f8f666b5749dcb90dcbd9081cd30`，仅保留核心、依赖头文件和测试，文件未修改。
- SpeexDSP：[Xiph 1.2.1 源码](https://downloads.xiph.org/releases/speex/speexdsp-1.2.1.tar.gz)，SHA-256 `8c777343e4a6399569c72abc38a95b24db56882c83dbdb6c6424a5f4aeb54d3d`。
- [测试组合许可](TEST-LICENSE.md)：含 Ring Out 的测试组合作为 GPL-3.0-or-later 组合分发，不能把 DLL 分开便当作免除许可义务。原有自有源码保留 MIT 授权；上游文件保留各自声明。

Actions 的测试包附完整许可、当前项目源码归档和 SpeexDSP 原始源码。完整项目交付目录已经包含公开仓库源码时，使用该相邻源码和构建脚本，无需再保留重复源码归档。
