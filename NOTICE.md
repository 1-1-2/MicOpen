# 第三方组件说明

MicLink Open 的原创代码使用 [MIT 许可](LICENSE)。第三方组件继续适用各自的许可和版权声明。

## WO Mic 驱动

- 项目：WO Mic。
- 提供方：Wolicheng Tech，按[官网标识](https://wolicheng.com/about.html)记录。
- 官网：<https://wolicheng.com/womic/>。

Windows 接收端使用电脑上已安装的 WO Mic 虚拟麦克风驱动。该驱动不是本项目原创代码，其许可不随本项目的 MIT 许可改变。公开源码不包含第三方 SYS、INF 或 CAT，也不包含官方安装包。构建工具支持维护者使用自备驱动目录进行本地安装测试；这种能力本身不授予第三方驱动的再分发权限。

## Opus

Android 编码和 Windows 解码可使用 Xiph.Org 的 libopus，适用 BSD 3-Clause 许可。分发包含该依赖的构建产物时，应保留对应版本源码附带的版权和许可文本。项目入口：<https://opus-codec.org/>。

## Microsoft 示例代码

归档的 `self-driver` 代码使用过 `microsoft/Windows-driver-samples` 中的音频示例。该部分适用 Microsoft 原始 MIT 许可；当前 Android/Windows 用户态产品不依赖这条驱动实现路线。

来源：<https://github.com/microsoft/Windows-driver-samples>，基线提交 `2dc3fd3a0cc84a2933f2194e7ec0871584979071`。

Copyright (c) Microsoft Corporation.

> Permission is hereby granted, free of charge, to any person obtaining a copy
> of this software and associated documentation files (the "Software"), to deal
> in the Software without restriction, including without limitation the rights
> to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
> copies of the Software, and to permit persons to whom the Software is
> furnished to do so, subject to the following conditions:
>
> The above copyright notice and this permission notice shall be included in all
> copies or substantial portions of the Software.
>
> THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
> IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
> FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
> AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
> LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
> OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
> SOFTWARE.

## 可选监听测试模块

Windows 监听测试组合使用 Dusk Audio Ring Out（GPL-3.0-or-later）与 Xiph SpeexDSP 1.2.1（BSD 类许可）。完整来源、固定版本与对应源码分发方式见 [监听模块说明](client/monitor/README.md) 和 [测试组合许可](client/monitor/TEST-LICENSE.md)。原有自有源码保留 MIT；含 Ring Out 的二进制测试组合按 GPL-3.0-or-later 分发。
