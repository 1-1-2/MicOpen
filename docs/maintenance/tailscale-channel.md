# 双机 CI 网络通道

`lab.yml` 的 `dual` 场景 用于在隔离的 CI runner 上验证 Android app 与 Windows 客户端之间的真实协议链路。该 workflow 面向自动化验证，不是产品运行时依赖。

## 运行模型

1. `tailscale/github-action@v4` 使用仓库 Secrets 中的 OAuth 客户端，把 runner 以 `tag:ci` 身份加入 tailnet；节点设为 ephemeral。
2. runner 发现联调节点后运行 TCP/UDP 探针，执行真实客户端与 Android app 的跨网验证。
3. 作业完成或超时后，官方 action 的收尾步骤登出并清理临时节点；证据只保存本次验证需要的日志和音频统计。

具体作业步骤以 [Lab](../../.github/workflows/lab.yml) 为准。不要把 runner 地址或临时节点身份写进长期文档。

## 仓库配置

凭据通过 GitHub Actions Secrets 注入，变量名为 `TS_OAUTH_CLIENT_ID` 与 `TS_OAUTH_SECRET`。OAuth 客户端需要可创建节点的 `auth_keys` scope，并允许 `tag:ci`。工作流不再自行调用 OAuth API 或创建 auth key；不要复制实际 secret、临时 auth key、节点 key 或 SSH 私钥到仓库、日志或 CI artifact。

action 会在 runner 安装 Tailscale 并建立 tailnet 连接。节点信息在 tailnet 内传播需要短暂时间，因此 workflow 仍按当前 run 的 hostname 和在线状态轮询发现对端。官方 action 的 `post` 阶段负责登出；节点的 ephemeral 属性让其随后自动清理。

为 tailnet 使用独立的 CI 身份和最小权限策略。只开放联调所需的端口和节点；测试结束后撤销长期测试密钥、清理遗留节点，并检查 ACL 是否仍符合当前工作流。

## 排查顺序

- 确认本次 workflow 使用了预期 ref，并且必要的 Actions Secrets 已配置。
- 查看 job 是否完成 runner 入网、节点发现、控制通道握手和媒体包接收四个阶段。
- 若节点发现失败，先检查临时节点是否在线、ACL 是否允许必要端口；不要在失败日志中输出完整凭据或访问令牌。
- 若控制通道成功但媒体无包，区分 UDP 路由/防火墙问题与 Android 采集/Opus 编码问题。
- 结束调试后确认作业进程与临时节点已退出。

## 证据边界

双机 workflow 验证的是本仓库 app 与客户端之间的链路。它不替代真实 Android 手机测试、官方 WO Mic Windows Client 互操作，也不证明小时级稳定性。

场景选择、双机暂停和重跑约束见[工作流使用说明](../../.github/workflows/README.md)。
