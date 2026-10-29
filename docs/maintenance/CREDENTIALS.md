# 凭据管理

仓库只记录凭据变量名，不保存凭据值。实际 token、OAuth secret 和私钥只由凭据管理器或 GitHub Actions Secrets 注入。

| 用途 | 配置名 | 保存位置 |
|---|---|---|
| GitHub API 写入工具 | `GH_TOKEN` | 受控进程环境/凭据管理器 |
| 双机 CI 入网 | `TS_OAUTH_CLIENT_ID`、`TS_OAUTH_SECRET` | GitHub Actions Secrets |
| CI 调试公钥 | `UPTERM_DEBUG_PUBKEY` | GitHub Actions Variables；只放公钥 |

双机工作流使用 CI 专用 OAuth 客户端：允许写入 `auth_keys`，签发标签为 `tag:ci`。`TS_OAUTH_CLIENT_ID` 和 `TS_OAUTH_SECRET` 必须来自同一客户端。管理客户端即使也有 `auth_keys` 权限，允许签发的标签仍可能不同，不能直接替换 CI 凭据。本地没有 CI secret 副本，不代表 GitHub Actions 未配置它；应通过工作流的入网结果核验。

## 待办：CI OAuth 凭据更新

2026-10-29 作者决定暂缓处理。**下次需要使用这组凭据前，先提醒并询问作者，不自动继续迁移或轮换。**

- [ ] 作者在 Tailscale 新建 CI 专用 OAuth 客户端：`auth_keys` 写权限、`tag:ci`。
- [ ] 将配套的新 ID、secret 同时更新到 MicOpen 的 `TS_OAUTH_CLIENT_ID` 和 `TS_OAUTH_SECRET`。
- [ ] 获得作者确认后验证入网；确认旧客户端不再被使用后，再由作者决定是否撤销。

SSH 公钥变量 `UPTERM_DEBUG_PUBKEY` 已配置并核对；管理用 OAuth 不在本次更新范围内。此待办不阻塞普通构建，也不表示 Lab 已经验证可用。

## 保存与使用规则

- 不把 token 放到命令行参数、URL、源代码、文档、截图、日志或 Git 历史。
- `tools/api_push.py` 只从 `GH_TOKEN` 环境变量读取凭据；见 [`github-api-push.md`](github-api-push.md)。
- 发现凭据进入文件或提交时，先在签发平台撤销/轮换，再从当前文件、日志、归档和可达历史中清理。
- 不在报告中复述旧 token、失效 token 片段、私有代理地址、OAuth 标识或私钥路径。
