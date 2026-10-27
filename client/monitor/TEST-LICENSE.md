# 含 Ring Out 的监听测试组合

Ring Out 核心和适配 `ringout.cpp` 采用 GPL-3.0-or-later。含该模块一起分发的 MicLink 监听测试组合作为 GPL-3.0-or-later 组合提供，对应完整正文见 `RingOut-GPL-3.0.txt`（源码位置为 `vendor/ringout/LICENSE`）。动态加载不会被用来规避这个要求。

MicLink 原有文件和新写的通用接口、宿主、SpeexDSP 适配保留原 MIT 授权及声明，MIT 授权与上述组合分发兼容。SpeexDSP 和 Opus 保留自己的 BSD 类许可，分别附 `SpeexDSP-COPYING.txt`、`Opus-COPYING.txt`。本说明没有更改第三方文件的著作权或原有许可。

公开测试二进制附件中包含 `monitor-source.tar.gz`（完整项目对应源码、适配和构建脚本）以及 `speexdsp-1.2.1.tar.gz`（原始上游源码）。在完整整理交付目录中，同一份对应源码位于相邻的 `miclink-open` 仓库；模块对应版本由测试记录和 Git 提交明确记录。构建脚本固定 Ring Out 提交和 SpeexDSP 下载校验值。

不附带上游完整插件的 GUI/DAW 框架，不代表上游为本适配或实际声学效果背书。软件按许可原样提供，不附加性能或适用性保证。
