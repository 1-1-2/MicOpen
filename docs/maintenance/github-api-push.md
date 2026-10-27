# Git Data API 推送工具

`tools/api_push.py` 是普通 Git 推送不可用时的后备通道。它读取本地提交，将 blob、tree 和 commit 写入 GitHub API，再更新目标分支。优先使用 `git push`，只在网络限制确实需要时使用本工具。

## 凭据与配置

- `GH_TOKEN`：从凭据管理器或短时环境注入；不要作为命令行参数传递。
- `GH_API_BASE`：可选，默认 `https://api.github.com`。必须是无 userinfo、无 query/fragment 的 HTTPS 根地址。
- 工具只输出 API 状态码和提交摘要，不输出响应正文或 token。

```bash
export GH_TOKEN="<从凭据管理器注入的 token>"
python3 tools/api_push.py main
```

## 历史重写与删除

`--force` 会从本地根提交重放历史并强制更新远端分支；`--delete <branch>` 会删除远端分支。只有在确认备份、目标分支和协作者状态后才使用。分支历史改写会改变 commit hash，也不会自动刷新其他克隆。

如果使用 GitHub 兼容 API 服务，确认该服务由仓库维护者批准，并将访问令牌放在进程环境中；不要把认证信息嵌进 API URL。网络受限不构成把私有代理地址或凭据写入仓库的理由。
