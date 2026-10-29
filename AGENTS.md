# 维护者约定

CI 专用 OAuth 凭据已按作者明确授权完成安全迁移和接口验证，见 [凭据状态](docs/maintenance/CREDENTIALS.md#ci-oauth-配置与待办)。不再执行此前暂缓的新建凭据计划，也不重复迁移。

下一次需要实际启用 Tailscale 入网、Lab 调试或双机联调前，先向作者确认场景；如果本次任务已经明确授权相应场景，不重复询问。不要自行新建或撤销 OAuth 客户端。普通构建和不依赖 Tailscale 的自动测试可继续执行。

无线直连修复与 Android 日志目前在 `feat/wifi-direct`，CI 通过不代替作者实机复测。作者确认可用前不创建新 Release。后续 Release Notes 保持简短，列用户相关改动、必要依赖和已知限制；完整验证记录放文档链接。
