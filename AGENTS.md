# 维护者约定

CI 专用 OAuth 凭据已按作者明确授权完成安全迁移和接口验证，见 [凭据状态](docs/maintenance/CREDENTIALS.md#ci-oauth-配置与待办)。不再执行此前暂缓的新建凭据计划，也不重复迁移。

下一次需要实际启用 Tailscale 入网、Lab 调试或双机联调前，先向作者确认场景；如果本次任务已经明确授权相应场景，不重复询问。不要自行新建或撤销 OAuth 客户端。普通构建和不依赖 Tailscale 的自动测试可继续执行。

无线直连修复与 Android 日志目前在 `feat/wifi-direct`，CI 通过不代替作者实机复测。作者确认可用前不创建新 Release。后续 Release Notes 保持简短，列用户相关改动、必要依赖和已知限制；完整验证记录放文档链接。

当前分支 `feat/wifi-direct-phone-initiated` 从 `221f54c` 分出，实施电脑等待／手机发起；旧 `feat/wifi-direct` 保留对照且不改写。见 docs/development/DIRECT-BRANCHES.md。未完成实体设备验收前不创建 Release；旧 CI 不代表新方向已验证。

`feat/direct-diagnostics` 从 `2b34e6f` 分出，增加建组诊断、Android 17 本地网络权限和 Windows A/B 会话路径。见 docs/development/DIRECT-DIAGNOSTICS.md。原生 UI、双 AVD 与真实 Windows 无线配对分别记录覆盖范围。
