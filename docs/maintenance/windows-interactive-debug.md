# Windows runner 交互调试

Windows 专项工作流可在产物下载后、驱动安装前或权限矩阵前后打开调试窗口。通过 `debug_before_a`、`debug_before_d`、`debug_before_g`、`debug_after_g` 选择插点，`debug_minutes` 设置时长。

`.github/actions/tailnet-debug` 负责 Windows 调试窗口的网络、OpenSSH 和公钥配置。SSH 公钥来自仓库 Variable `UPTERM_DEBUG_PUBKEY`；OAuth ID 和 secret 来自 Actions Secrets。公钥与 OAuth secret 用途不同，配置名见[凭据说明](CREDENTIALS.md)。

双机工作流使用官方 Tailscale Action 建立测试网络，可通过 `debug_hold` 打开两端 SSH 窗口。节点名包含当前运行编号，调试结束后窗口按时限释放，工作流继续验证。

`android.yml` 另有可选的 Windows/Linux 交互调试作业，其 action、账号授权与依赖以该 workflow 为准。自动化验证不依赖手工调试窗口；开启 SSH 窗口也不代表交互会话录音已验证。G2 的判定见[验证报告](../verification/windows-e2e-report.md)。
