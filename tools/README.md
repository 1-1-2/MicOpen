# 仓库工具

## `api_push.py`

当普通 `git push` 不适用于当前网络环境时，可用 GitHub Git Data API 按提交重放并同步仓库。正常情况下优先使用 Git 本身的 HTTPS/SSH 推送。

工具把凭据从 `GH_TOKEN` 环境变量读取，不接受 token 命令行参数，也不会打印 API 响应正文。默认 API 根地址为 `https://api.github.com`；确需使用兼容 API 服务时，通过 `GH_API_BASE` 配置无用户名、密码、查询参数或片段的 HTTPS 地址。

```bash
export GH_TOKEN="<从凭据管理器注入的 token>"
python3 tools/api_push.py main
```

`--force` 会重放本地提交并改写远端分支历史；`--delete <branch>` 会删除远端分支。执行前应确认目标仓库、分支和备份，避免覆盖其他人的提交。不要把 token 放进命令参数、shell 历史或文档。

## 其他构建工具

Windows 客户端构建与打包工具位于 [`../client/tools/`](../client/tools)，Android Opus 准备脚本位于 [`../.github/scripts/fetch-opus.sh`](../.github/scripts/fetch-opus.sh)。构建输出与第三方依赖不进入版本库。
