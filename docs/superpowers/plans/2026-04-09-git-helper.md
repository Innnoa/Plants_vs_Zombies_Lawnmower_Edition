# Git Helper Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** 为仓库新增一个统一的 git helper 脚本，支持交互菜单与子命令，标准化 `status`、`pull`、`push`、`commit`、`switch`、`log`、`stash` 的常用操作。

**Architecture:** 以 `script/git_helper.sh` 作为唯一入口，所有菜单项和命令行子命令都分发到同一套 `run_*` 函数，避免两套逻辑漂移。新增 `script/test_git_helper.sh`，在临时 git 仓库和本地 bare remote 中做 bash smoke test，覆盖参数校验、只读命令、写命令、远端同步和菜单入口。

**Tech Stack:** Bash, Git CLI, Markdown

---

## File Map

- `script/git_helper.sh`
  - 主入口脚本
  - 负责仓库根目录定位、参数解析、菜单显示和 git 命令分发
- `script/test_git_helper.sh`
  - bash smoke test
  - 在临时 git 仓库中验证帮助输出、只读命令、写命令、remote pull/push 与菜单入口
- `README.md`
  - 增加 git helper 的最小使用说明

### Task 1: Add CLI Smoke Test And Read-Only Skeleton

**Files:**
- Create: `script/test_git_helper.sh`
- Create: `script/git_helper.sh`

- [ ] **Step 1: Write the failing smoke test**

先写一个最小 bash smoke test，只覆盖第一阶段必须成立的行为：

- `--help` 输出中文帮助
- `status` 能显示 `git status --short --branch`
- `log` 能显示最近提交
- `stash list` 在空仓库场景返回空结果
- `commit` 缺少消息时报错
- `switch` 缺少分支名时报错

测试文件内容：

```bash
#!/usr/bin/env bash
set -euo pipefail

repo_root="$(cd "$(dirname "$0")/.." && pwd)"
helper_source="$repo_root/script/git_helper.sh"
helper=""

fail() {
  echo "FAIL: $*" >&2
  exit 1
}

assert_contains() {
  local haystack="$1"
  local needle="$2"
  [[ "$haystack" == *"$needle"* ]] || fail "expected output to contain [$needle]"
}

assert_command_fails() {
  set +e
  "$@"
  local status=$?
  set -e
  [[ $status -ne 0 ]] || fail "expected command to fail: $*"
}

tmp_repo="$(mktemp -d)"
remote_root=""
cleanup() {
  rm -rf "$tmp_repo"
  if [[ -n "$remote_root" ]]; then
    rm -rf "$remote_root"
  fi
}
trap cleanup EXIT

git -C "$tmp_repo" init -b main >/dev/null
git -C "$tmp_repo" config user.name "Test User"
git -C "$tmp_repo" config user.email "test@example.com"
printf 'seed\n' > "$tmp_repo/app.txt"
git -C "$tmp_repo" add app.txt
git -C "$tmp_repo" commit -m "chore: seed repo" >/dev/null

mkdir -p "$tmp_repo/script"
[[ -f "$helper_source" ]] || fail "helper script not found: $helper_source"
cp "$helper_source" "$tmp_repo/script/git_helper.sh"
chmod +x "$tmp_repo/script/git_helper.sh"
helper="$tmp_repo/script/git_helper.sh"

help_output="$(cd "$tmp_repo" && "$helper" --help)"
assert_contains "$help_output" "用法"
assert_contains "$help_output" "status"
assert_contains "$help_output" "commit -m"

status_output="$(cd "$tmp_repo" && "$helper" status)"
assert_contains "$status_output" "## main"

log_output="$(cd "$tmp_repo" && "$helper" log)"
assert_contains "$log_output" "chore: seed repo"

stash_output="$(cd "$tmp_repo" && "$helper" stash list)"
[[ -z "$stash_output" ]] || fail "expected stash list to be empty"

assert_command_fails bash -lc "cd '$tmp_repo' && '$helper' commit"
assert_command_fails bash -lc "cd '$tmp_repo' && '$helper' switch"

echo "git_helper read-only smoke test: PASS"
```

- [ ] **Step 2: Run the smoke test to verify it fails**

Run: `bash script/test_git_helper.sh`

Expected: FAIL because `script/git_helper.sh` does not exist yet, so测试脚本会在复制 helper 时失败。

- [ ] **Step 3: Write the minimal CLI skeleton**

创建 `script/git_helper.sh`，先只实现：

- 帮助输出
- 仓库根目录定位
- `status`
- `log`
- `stash list`
- `commit` / `switch` 的参数校验

脚本骨架：

```bash
#!/usr/bin/env bash
set -euo pipefail

usage() {
  cat <<'EOF'
用法:
  script/git_helper.sh [menu|status|pull|push|commit|switch|log|stash|help]

子命令:
  menu                       打开交互菜单
  status                     查看当前状态
  pull                       执行 git pull --ff-only
  push                       执行 git push
  commit -m "message"        暂存全部改动后提交
  switch <branch>            切换到已有本地分支
  log                        查看最近 15 条提交
  stash list                 查看 stash 列表
  stash push [-m "message"]  保存当前修改
  stash pop                  恢复最近一次 stash
  help                       显示帮助
EOF
}

script_dir="$(cd "$(dirname "$0")" && pwd)"
repo_root="$(cd "$script_dir/.." && pwd)"

ensure_git_repo() {
  if ! git rev-parse --is-inside-work-tree >/dev/null 2>&1; then
    echo "当前目录不在 git 仓库内。" >&2
    exit 1
  fi
}

run_status() {
  git status --short --branch
}

run_log() {
  git log --oneline --decorate -n 15
}

run_stash() {
  local action="${1:-list}"
  case "$action" in
    list)
      git stash list
      ;;
    *)
      echo "暂未实现的 stash 子命令: $action" >&2
      return 1
      ;;
  esac
}

run_commit() {
  local message="$1"
  if [[ -z "$message" ]]; then
    echo "commit 需要通过 -m 或 --message 提供提交信息。" >&2
    return 1
  fi
  echo "commit 子命令将在下一步实现。" >&2
  return 1
}

run_switch() {
  local branch="$1"
  if [[ -z "$branch" ]]; then
    echo "switch 需要目标分支名。" >&2
    return 1
  fi
  echo "switch 子命令将在下一步实现。" >&2
  return 1
}

main() {
  local command="${1:-help}"
  shift || true

  case "$command" in
    help|-h|--help)
      usage
      ;;
    status)
      run_status
      ;;
    log)
      run_log
      ;;
    stash)
      run_stash "${1:-list}"
      ;;
    commit)
      local message=""
      while (($#)); do
        case "$1" in
          -m|--message)
            message="${2:-}"
            shift 2
            ;;
          *)
            echo "未知 commit 参数: $1" >&2
            return 1
            ;;
        esac
      done
      run_commit "$message"
      ;;
    switch)
      run_switch "${1:-}"
      ;;
    *)
      echo "未知子命令: $command" >&2
      usage >&2
      return 1
      ;;
  esac
}

cd "$repo_root"
ensure_git_repo
main "$@"
```

- [ ] **Step 4: Mark executable and run the smoke test again**

Run: `chmod +x script/git_helper.sh script/test_git_helper.sh && bash script/test_git_helper.sh`

Expected: PASS with `git_helper read-only smoke test: PASS`

- [ ] **Step 5: Commit**

```bash
git add script/git_helper.sh script/test_git_helper.sh
git commit -m "feat: add git helper cli skeleton"
```

### Task 2: Implement Local Write Commands

**Files:**
- Modify: `script/git_helper.sh`
- Modify: `script/test_git_helper.sh`

- [ ] **Step 1: Extend the smoke test with failing local write cases**

在 `script/test_git_helper.sh` 追加本地写操作验证：

- `commit -m` 会先 `git add -A` 再创建新提交
- `switch <branch>` 能切换到已有分支
- `stash push -m` 会生成 stash
- `stash pop` 会恢复工作区内容

追加测试片段：

```bash
git -C "$tmp_repo" switch -c feature/demo >/dev/null
git -C "$tmp_repo" switch main >/dev/null

printf 'tracked change\n' >> "$tmp_repo/app.txt"
(cd "$tmp_repo" && "$helper" commit -m "feat: helper commit" >/dev/null)
latest_subject="$(git -C "$tmp_repo" log --oneline -n 1)"
assert_contains "$latest_subject" "feat: helper commit"

(cd "$tmp_repo" && "$helper" switch feature/demo >/dev/null)
current_branch="$(git -C "$tmp_repo" branch --show-current)"
[[ "$current_branch" == "feature/demo" ]] || fail "expected branch feature/demo"

printf 'stash me\n' >> "$tmp_repo/app.txt"
(cd "$tmp_repo" && "$helper" stash push -m "wip: test stash" >/dev/null)
stash_after_push="$(git -C "$tmp_repo" stash list)"
assert_contains "$stash_after_push" "wip: test stash"

(cd "$tmp_repo" && "$helper" stash pop >/dev/null)
grep -F "stash me" "$tmp_repo/app.txt" >/dev/null || fail "expected stash pop to restore content"
```

- [ ] **Step 2: Run the smoke test to verify it fails**

Run: `bash script/test_git_helper.sh`

Expected: FAIL because `run_commit` / `run_switch` / `stash push` / `stash pop` are still unimplemented.

- [ ] **Step 3: Implement the local write commands**

把 `script/git_helper.sh` 的写操作补齐为最小实现：

```bash
run_commit() {
  local message="$1"
  if [[ -z "$message" ]]; then
    echo "commit 需要通过 -m 或 --message 提供提交信息。" >&2
    return 1
  fi

  git status --short
  git add -A
  git commit -m "$message"
}

run_switch() {
  local branch="$1"
  if [[ -z "$branch" ]]; then
    echo "switch 需要目标分支名。" >&2
    return 1
  fi

  git show-ref --verify --quiet "refs/heads/$branch" || {
    echo "本地分支不存在: $branch" >&2
    return 1
  }

  git switch "$branch"
}

run_stash() {
  local action="${1:-list}"
  shift || true

  case "$action" in
    list)
      git stash list
      ;;
    push)
      local message=""
      while (($#)); do
        case "$1" in
          -m|--message)
            message="${2:-}"
            shift 2
            ;;
          *)
            echo "未知 stash push 参数: $1" >&2
            return 1
            ;;
        esac
      done

      if [[ -n "$message" ]]; then
        git stash push -m "$message"
      else
        git stash push
      fi
      ;;
    pop)
      git stash pop
      ;;
    *)
      echo "不支持的 stash 子命令: $action" >&2
      return 1
      ;;
  esac
}
```

- [ ] **Step 4: Run the smoke test to verify it passes**

Run: `bash script/test_git_helper.sh`

Expected: PASS with both read-only and local write checks through.

- [ ] **Step 5: Commit**

```bash
git add script/git_helper.sh script/test_git_helper.sh
git commit -m "feat: add git helper local write commands"
```

### Task 3: Implement Remote Sync Commands And Interactive Menu

**Files:**
- Modify: `script/git_helper.sh`
- Modify: `script/test_git_helper.sh`

- [ ] **Step 1: Extend the smoke test with failing remote and menu cases**

在 `script/test_git_helper.sh` 追加 remote 与菜单场景。测试需要构造本地 bare remote、两个 clone，并验证：

- `push` 走当前 upstream
- `pull` 走 `--ff-only`
- `menu` 模式可进入并正常退出

追加测试片段：

```bash
remote_root="$(mktemp -d)"
remote_repo="$remote_root/origin.git"
alice_repo="$remote_root/alice"
bob_repo="$remote_root/bob"

git init --bare "$remote_repo" >/dev/null
git clone "$remote_repo" "$alice_repo" >/dev/null 2>&1
git -C "$alice_repo" config user.name "Test User"
git -C "$alice_repo" config user.email "test@example.com"
git -C "$alice_repo" switch -c main >/dev/null
printf 'remote seed\n' > "$alice_repo/shared.txt"
git -C "$alice_repo" add shared.txt
git -C "$alice_repo" commit -m "chore: seed remote" >/dev/null
git -C "$alice_repo" push -u origin main >/dev/null

git clone "$remote_repo" "$bob_repo" >/dev/null 2>&1
git -C "$bob_repo" config user.name "Test User"
git -C "$bob_repo" config user.email "test@example.com"

printf 'push from helper\n' >> "$bob_repo/shared.txt"
git -C "$bob_repo" commit -am "feat: helper push" >/dev/null
(cd "$bob_repo" && "$helper" push >/dev/null)
git -C "$alice_repo" pull --ff-only >/dev/null
git -C "$alice_repo" log --oneline -n 1 | grep -F "feat: helper push" >/dev/null || fail "expected helper push to update remote"

printf 'pull target\n' >> "$alice_repo/shared.txt"
git -C "$alice_repo" commit -am "feat: helper pull" >/dev/null
git -C "$alice_repo" push >/dev/null
(cd "$bob_repo" && "$helper" pull >/dev/null)
git -C "$bob_repo" log --oneline -n 1 | grep -F "feat: helper pull" >/dev/null || fail "expected helper pull to fast-forward"

menu_output="$(printf '8\n' | (cd "$tmp_repo" && "$helper" menu))"
assert_contains "$menu_output" "Git 操作菜单"
```

- [ ] **Step 2: Run the smoke test to verify it fails**

Run: `bash script/test_git_helper.sh`

Expected: FAIL because `pull` / `push` / `menu` are still missing.

- [ ] **Step 3: Implement remote sync commands and menu dispatch**

在 `script/git_helper.sh` 增加：

```bash
run_pull() {
  git pull --ff-only
}

run_push() {
  git push
}

prompt_commit() {
  local message=""
  read -r -p "请输入提交信息: " message
  run_commit "$message"
}

prompt_switch() {
  echo "本地分支列表:"
  git branch --format='  %(refname:short)'
  local branch=""
  read -r -p "请输入要切换的分支名: " branch
  run_switch "$branch"
}

prompt_stash() {
  echo "1) stash push"
  echo "2) stash pop"
  echo "3) stash list"
  local action=""
  read -r -p "请选择 stash 操作 [1-3]: " action
  case "$action" in
    1)
      local message=""
      read -r -p "请输入 stash 说明（可留空）: " message
      if [[ -n "$message" ]]; then
        run_stash push -m "$message"
      else
        run_stash push
      fi
      ;;
    2)
      run_stash pop
      ;;
    3)
      run_stash list
      ;;
    *)
      echo "无效的 stash 选项: $action" >&2
      return 1
      ;;
  esac
}

show_menu() {
  while true; do
    cat <<'EOF'
=== Git 操作菜单 ===
1) 查看状态
2) 拉取更新
3) 推送当前分支
4) 暂存全部并提交
5) 切换分支
6) 查看日志
7) Stash 操作
8) 退出
EOF

    local choice=""
    read -r -p "请选择操作 [1-8]: " choice

    case "$choice" in
      1) run_status ;;
      2) run_pull ;;
      3) run_push ;;
      4) prompt_commit ;;
      5) prompt_switch ;;
      6) run_log ;;
      7) prompt_stash ;;
      8) return 0 ;;
      *)
        echo "无效选项: $choice" >&2
        ;;
    esac
    echo
  done
}
```

同时更新 `main` 分发：

```bash
main() {
  local command="${1:-menu}"
  shift || true

  case "$command" in
    menu)
      show_menu
      ;;
    help|-h|--help)
      usage
      ;;
    status)
      run_status
      ;;
    pull)
      run_pull
      ;;
    push)
      run_push
      ;;
    log)
      run_log
      ;;
    stash)
      run_stash "$@"
      ;;
    commit)
      local message=""
      while (($#)); do
        case "$1" in
          -m|--message)
            message="${2:-}"
            shift 2
            ;;
          *)
            echo "未知 commit 参数: $1" >&2
            return 1
            ;;
        esac
      done
      run_commit "$message"
      ;;
    switch)
      run_switch "${1:-}"
      ;;
    *)
      echo "未知子命令: $command" >&2
      usage >&2
      return 1
      ;;
  esac
}
```

- [ ] **Step 4: Run the smoke test to verify it passes**

Run: `bash script/test_git_helper.sh`

Expected: PASS with remote pull/push and menu checks through.

- [ ] **Step 5: Commit**

```bash
git add script/git_helper.sh script/test_git_helper.sh
git commit -m "feat: add git helper menu and remote commands"
```

### Task 4: Update README And Run Final Verification

**Files:**
- Modify: `README.md`
- Reference: `script/git_helper.sh`
- Reference: `script/test_git_helper.sh`

- [ ] **Step 1: Add the README section**

在 `README.md` 的“测试网络通信”后面增加一个简短的 git helper 使用说明：

````md
### 常用 Git 脚本

仓库提供统一的 git 操作脚本：

```bash
# 打开交互菜单
script/git_helper.sh

# 查看状态
script/git_helper.sh status

# 暂存全部并提交
script/git_helper.sh commit -m "docs: update readme"

# 拉取更新（fast-forward only）
script/git_helper.sh pull
```

默认支持的子命令：

- `status`
- `pull`
- `push`
- `commit -m "message"`
- `switch <branch>`
- `log`
- `stash push|pop|list`
````

- [ ] **Step 2: Run syntax checks**

Run: `bash -n script/git_helper.sh && bash -n script/test_git_helper.sh`

Expected: PASS with no output.

- [ ] **Step 3: Run full smoke verification**

Run: `bash script/test_git_helper.sh && ./script/git_helper.sh --help`

Expected:

- smoke test prints `PASS`
- help 输出包含 `pull --ff-only` 和 `stash push`

- [ ] **Step 4: Review the final diff before commit**

Run: `git diff -- script/git_helper.sh script/test_git_helper.sh README.md`

Expected: 只包含本轮 git helper 和 README 相关改动，没有混入其他业务文件。

- [ ] **Step 5: Commit**

```bash
git add script/git_helper.sh script/test_git_helper.sh README.md
git commit -m "feat: add git helper script"
```
