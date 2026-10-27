# Git 提交规范

## 身份

本仓库的作者和提交者统一使用 GitHub 账号 `1-1-2` 的 noreply 邮箱：

```text
user.name  = 1-1-2
user.email = 50569812+1-1-2@users.noreply.github.com
```

在当前克隆中设置：

```bash
git config --local user.name "1-1-2"
git config --local user.email "50569812+1-1-2@users.noreply.github.com"
```

作者/提交者字段说明提交归属；它们本身不构成密码学签名，也不等同于 GitHub 的 “Verified”。需要签名时，另行配置个人 SSH 或 GPG signing key，不共享 agent 的密钥。

## 提交说明

标题格式：`type(scope): 中文摘要`。常用类型为 `feat`、`fix`、`docs`、`refactor`、`test`、`build`、`ci`、`chore`。

示例：

```text
fix(android): 切页时保留页面状态
docs(protocol): 补充 UDP 包字段说明
```

每个提交围绕一个可单独审阅、回滚的结果。跨平台同一功能可以合并在一个提交中；独立修复、文档改写和构建配置变更应分开。正文只补充背景、行为变化或验证范围，不重复标题，也不写无关任务编号清单。

## 历史维护

- 已推送历史若要重写，先制作备份并明确协调远端分支；重写会改变提交 hash。
- 不把用户名改写描述成签名验证。保持作者/提交者的准确归属，不伪造签名状态。
- 提交前检查 staged diff、凭据文件、生成物和 `.gitignore` 命中项；不要提交 token、私钥、构建缓存或本机配置。
- 历史过滤后逐提交核对代码内容、作者和时间，并保留提交映射供维护者审阅。

## 文档格式

自然段在 Markdown 源文件中保持一行，由编辑器和渲染页面自动折行。列表按条目分行，表格、代码块和 Mermaid 保留各自的结构。关系图使用 Mermaid，原始字节样例和命令输出保留代码块。
