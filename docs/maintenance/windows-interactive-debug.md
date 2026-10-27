# Windows runner 交互调试

在 **Lab** 工作流中选择基础环境、工具链、成品、安装完成或验证现场。验证现场保留测试开始前、安装驱动前、权限矩阵前后和故障后的暂停能力。参数、网页操作、API 命令和结束方式见[工作流使用说明](../../.github/workflows/README.md)。

网络由官方 Tailscale Action 建立；`.github/actions/lab-session` 复用已建立的网络并配置普通 OpenSSH，不会再次入网。无法从操作端加入 tailnet 时，可显式选 `ssh_transport=relay`，通过 WSS 中继进入同一个原生 OpenSSH；默认不启用。SSH 公钥来自仓库 Variable `UPTERM_DEBUG_PUBKEY`，OAuth 配置见[凭据说明](CREDENTIALS.md)。

`stream_logs` 默认关闭。开启后，仅通过记录工具执行的操作经过处理后进入公开日志；普通 SSH 输出不会自动成为 Actions 日志。Windows G2 录音判定与 SSH 是否可连接是不同问题，见[验证报告](../verification/windows-e2e-report.md)。
