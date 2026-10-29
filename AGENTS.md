# 维护者约定

在需要使用 CI 专用 OAuth 凭据进行 Tailscale 入网、Lab 调试或双机联调之前，先阅读 [凭据待办](docs/maintenance/CREDENTIALS.md#待办ci-oauth-凭据更新)，向作者确认是否已完成新建和配置，再继续使用。

这项待办目前暂停；不要自行迁移历史凭据、新建或撤销 OAuth 客户端。普通构建和不依赖 Tailscale 的自动测试可继续执行。
