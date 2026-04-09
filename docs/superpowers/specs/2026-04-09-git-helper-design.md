# 2026-04-09 Git Helper Design

## Summary

本设计用于给当前仓库新增一个统一的 git 操作入口，目标是：

- 把常用 git 操作收敛到 `script/git_helper.sh`
- 同时支持交互菜单和命令行子命令
- 保持和现有 `script/*.sh` 一致的 `bash` 风格、中文帮助信息与仓库根目录定位方式

本轮只覆盖常用日常操作，不试图把复杂 git 工作流自动化。

## Current State

当前仓库已有多个维护脚本，均放在 `script/` 下：

- `script/build_server.sh`
- `script/check_connectivity.sh`
- `script/format_server.sh`

这些脚本有几个稳定特征：

- 使用 `#!/usr/bin/env bash`
- 统一启用严格模式
- 通过脚本所在目录反推仓库根目录
- 提供中文 `--help`

与此相对，当前仓库还没有统一的 git 操作脚本。常见 git 动作仍需手动拼命令，容易出现：

- 命令记忆负担高
- 每次执行方式不一致
- 安全边界依赖操作者临时判断

另外，仓库实际使用的脚本目录是 `script/`，不是 `scripts/`，因此本轮应沿用现有目录而不是新增第二套目录约定。

## Approach Options

### Option A: 单脚本双入口

做法：

- 新增一个 `script/git_helper.sh`
- 同时支持交互菜单与命令行子命令
- 菜单与命令行共用同一套内部函数

优点：

- 单一入口，维护成本最低
- 菜单操作与脚本化调用都可覆盖
- 行为更容易保持一致

缺点：

- 单文件会比普通脚本更长

### Option B: 菜单脚本 + 多个独立 git 子脚本

做法：

- 菜单只负责分发
- 每个 git 动作拆成独立脚本

优点：

- 单个文件更短

缺点：

- 公共逻辑会重复
- 文件数量增长快
- 菜单模式和命令行模式更容易分叉

### Option C: 只做交互菜单

做法：

- 直接运行脚本进入菜单
- 不支持命令行子命令

优点：

- 实现最简单

缺点：

- 不能被其他脚本复用
- 不符合“每次选择脚本选项，同时也支持参数调用”的目标

## Recommendation

采用 `Option A`。

原因：

- 最符合“标准化入口”的目标
- 同时满足菜单交互和命令行调用
- 可以把安全默认值集中在一个地方定义

## Design

### 1. File Layout

新增：

- `script/git_helper.sh`

本轮不新增新的脚本目录，不拆分子脚本。

### 2. Command Surface

脚本需要支持以下两种入口：

1. 交互菜单
   - 直接运行 `script/git_helper.sh`
   - 或运行 `script/git_helper.sh menu`

2. 命令行子命令
   - `script/git_helper.sh status`
   - `script/git_helper.sh pull`
   - `script/git_helper.sh push`
   - `script/git_helper.sh commit -m "message"`
   - `script/git_helper.sh switch <branch>`
   - `script/git_helper.sh log`
   - `script/git_helper.sh stash push [-m "message"]`
   - `script/git_helper.sh stash pop`
   - `script/git_helper.sh stash list`
   - `script/git_helper.sh help`
   - `script/git_helper.sh --help`

### 3. Internal Structure

脚本内部建议拆成以下函数：

- `usage`
- `ensure_git_repo`
- `run_status`
- `run_pull`
- `run_push`
- `run_commit`
- `run_switch`
- `run_log`
- `run_stash`
- `show_menu`
- `main`

规则：

- 所有菜单选项最终都调用上述 `run_*` 函数
- 命令行模式也只调用同一套 `run_*` 函数
- 禁止菜单模式与命令行模式维护两套独立实现

### 4. Repository Resolution

脚本需要和现有 `script/*.sh` 保持一致：

- 先根据脚本位置计算 `repo_root`
- 再切换到仓库根目录执行 git 命令

这样可以避免：

- 从子目录运行时输出不一致
- 菜单模式与命令行模式因当前工作目录不同而行为漂移

### 5. Operation Semantics

#### `status`

默认执行：

- `git status --short --branch`

目的：

- 快速显示当前分支
- 快速显示文件改动概况

#### `pull`

默认执行：

- `git pull --ff-only`

原因：

- 只允许快进更新
- 避免脚本自动产生 merge commit

本轮不支持：

- 自动 `rebase`
- 自动解决冲突

#### `push`

默认执行：

- `git push`

若当前分支没有 upstream：

- 明确报错并提示用户手动设置

本轮不支持：

- 自动推断并创建远程 upstream

#### `commit`

默认语义：

1. 显示 `git status --short`
2. 执行 `git add -A`
3. 读取提交信息
4. 执行 `git commit -m "<message>"`

约束：

- 提交信息为空时直接中止
- 第一版不提供交互式逐文件暂存
- 命令行模式必须支持 `-m` / `--message`
- 菜单模式可以直接提示用户输入消息

#### `switch`

默认语义：

- 菜单模式先展示本地分支列表，再提示输入目标分支
- 命令行模式支持 `switch <branch>`

本轮只切换已有分支，不自动创建新分支。

#### `log`

默认执行：

- `git log --oneline --decorate -n 15`

目的：

- 让脚本保留一个轻量历史查看入口

#### `stash`

第一版支持：

- `stash push`
- `stash pop`
- `stash list`

其中：

- `stash push` 支持可选说明
- 菜单模式下若选择 `stash push`，可提示输入说明

本轮不支持：

- `stash apply`
- `stash drop`
- `stash branch`

### 6. Menu Behavior

菜单项至少包含：

1. 查看状态
2. 拉取更新
3. 推送当前分支
4. 暂存全部并提交
5. 切换分支
6. 查看日志
7. Stash 操作
8. 退出

交互要求：

- 菜单文案使用中文
- 非法输入要提示后重试
- 用户主动取消时应返回非 `0`

### 7. Help Output

`--help` 需要明确给出：

- 脚本用途
- 可用子命令
- 每个子命令的参数
- 至少 2 个最小示例

帮助文本应和现有 `script/*.sh` 风格一致，优先中文说明。

### 8. Error Handling

脚本至少需要处理这些错误：

- 当前目录不在 git 仓库内
- 缺少必要参数，例如 `commit` 没有消息、`switch` 没有目标分支
- 用户菜单输入非法
- `git pull --ff-only` 失败
- `git push` 失败
- 目标分支不存在

处理原则：

- 错误信息直接、可读
- 不隐藏原始 git 失败
- 不替用户自动做高风险决策

### 9. Out of Scope

本轮明确不做：

- 自动 `rebase`
- 自动创建分支
- 自动设置 upstream
- 交互式逐文件 `add`
- 冲突解决辅助
- 批量多仓库操作
- 图形界面或 TUI 框架

## Testing

本轮实现必须至少覆盖以下验证：

1. 脚本语法
   - `bash -n script/git_helper.sh`

2. 帮助输出
   - `script/git_helper.sh --help`

3. 只读命令
   - `script/git_helper.sh status`
   - `script/git_helper.sh log`
   - `script/git_helper.sh stash list`

4. 参数校验
   - `script/git_helper.sh commit` 应因缺少消息失败
   - `script/git_helper.sh switch` 应因缺少分支名失败

5. 菜单路径
   - 至少验证菜单可进入并能正常退出

如果当前工作区条件允许，再补充一次真实 `commit` / `switch` / `stash push` 的临时仓库验证。

## Risks

- 如果菜单模式和命令行模式各写一套逻辑，后续行为容易漂移
- 如果 `pull` 默认不是 `--ff-only`，脚本可能在用户不注意时制造 merge commit
- 如果 `push` 自动猜 upstream，错误分支推送的风险较高
- 如果 `commit` 不预览状态，用户更难察觉 `git add -A` 的影响范围

## Mitigations

- 菜单和命令行共享同一套 `run_*` 实现
- `pull` 固定使用 `--ff-only`
- `push` 失败时保留 git 原始报错，不做额外猜测
- `commit` 前先显示 `git status --short`
