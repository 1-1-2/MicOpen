# 凭据管理

仓库只记录凭据变量名，不保存凭据值。运行时通过受控进程环境或 GitHub Actions Secrets 注入；作者明确授权的本地私有备份与 Git 仓库分开保管。

| 用途 | 配置名 | 保存位置 |
|---|---|---|
| GitHub API 写入工具 | `GH_TOKEN` | 受控进程环境/凭据管理器 |
| 双机 CI 入网 | `TS_OAUTH_CLIENT_ID`、`TS_OAUTH_SECRET` | GitHub Actions Secrets |
| CI 调试公钥 | `UPTERM_DEBUG_PUBKEY` | GitHub Actions Variables；只放公钥 |

双机工作流使用 CI 专用 OAuth 客户端：允许写入 `auth_keys`，签发标签为 `tag:ci`。`TS_OAUTH_CLIENT_ID` 和 `TS_OAUTH_SECRET` 必须来自同一客户端。管理客户端即使也有 `auth_keys` 权限，允许签发的标签仍可能不同，不能直接替换 CI 凭据。配置存在、OAuth 认证成功与实际入网是不同验证范围；入网结果应通过对应工作流核验。

## CI OAuth 配置与待办

2026-10-29 作者随后明确授权一次性安全迁移，已完成 MicOpen 的 CI 凭据配置。沿用原 CI OAuth 客户端，没有新建或撤销 Tailscale 客户端；此前的新建凭据计划已由本次迁移替代。

- [x] `TS_OAUTH_CLIENT_ID`、`TS_OAUTH_SECRET` 已配套配置到 MicOpen Actions Secrets。
- [x] [MicOpen 独立验证](https://github.com/1-1-2/MicOpen/actions/runs/40807940182)通过：CI 身份匹配、Tailscale OAuth 认证，以及 SSH 公钥变量存在性检查。
- [ ] 实际 Tailscale 入网、Lab SSH 或双机联调尚未重跑；下次需要启用时先确认场景。已经明确授权的场景不重复询问。

SSH 公钥变量 `UPTERM_DEBUG_PUBKEY` 已配置并核对；管理用 OAuth 不在本次更新范围内。迁移时仅传递密文，未导出本地明文；随后按作者单独授权，已将 CI secret 加密传回沙箱，解密后以 `0600` 权限保存在仓库外的私有凭据文件中。临时分支、加密产物与临时解密密钥已清理，没有在 Git、日志或公开产物中保存明文。

凭据配置及私有备份完成不等于实际 Lab / SSH 已验证可用，也不阻塞普通构建。

## 保存与使用规则

- 不把 token 放到命令行参数、URL、源代码、文档、截图、日志或 Git 历史。
- `tools/api_push.py` 只从 `GH_TOKEN` 环境变量读取凭据；见 [`github-api-push.md`](github-api-push.md)。
- 作者授权的本地明文备份仅保存在仓库外、权限为 `0600` 的私有文件中，不进入公开发布包。
- 发现凭据进入非预期文件、日志、公开产物或 Git 历史时，先在签发平台撤销/轮换，再清理泄露副本。
- 不在报告中复述旧 token、失效 token 片段、私有代理地址、OAuth 标识或私钥路径。
