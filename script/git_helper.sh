#!/usr/bin/env bash

set -euo pipefail

script_dir="$(cd "$(dirname "$0")" && pwd)"
repo_root="$(cd "$script_dir/.." && pwd)"

usage() {
  cat <<'EOF'
用法:
  script/git_helper.sh <命令> [参数]
  script/git_helper.sh              # 不带参数时默认进入 menu

命令:
  menu                 显示交互菜单
  status               显示 git status --short --branch
  pull                 拉取更新（git pull --ff-only）
  push                 推送当前分支（git push）
  commit -m <message>  暂存全部变更并提交（git add -A + git commit）
  switch <branch>      切换到已有本地分支
  log                  显示最近 15 条提交
  stash                支持: list / push / pop（push 使用默认行为，不包含额外未跟踪文件）
  help                 显示帮助

示例:
  script/git_helper.sh status
  script/git_helper.sh log
  script/git_helper.sh stash list
EOF
}

ensure_git_repo() {
  if ! git rev-parse --is-inside-work-tree >/dev/null 2>&1; then
    echo "当前目录不是 git 仓库：$repo_root" >&2
    exit 1
  fi
}

require_no_extra_args() {
  local command_name="$1"
  shift
  if (($# > 0)); then
    echo "$command_name 不接受额外参数。" >&2
    return 1
  fi
}

run_status() {
  require_no_extra_args "status" "$@"
  git status --short --branch
}

show_commit_preview() {
  git status --short
}

perform_commit() {
  local message="$1"
  git add -A
  git commit -m "$message"
}

run_log() {
  require_no_extra_args "log" "$@"
  git --no-pager log --oneline --decorate -n 15
}

run_stash() {
  if (($# == 0)); then
    git stash list
    return
  fi

  local subcommand="$1"
  shift

  case "$subcommand" in
    list)
      require_no_extra_args "stash list" "$@"
      git stash list
      ;;
    pop)
      require_no_extra_args "stash pop" "$@"
      git stash pop
      ;;
    push)
      local message=""
      while (($#)); do
        case "$1" in
          -m|--message)
            if (($# < 2)) || [[ -z "$2" ]]; then
              echo "stash push 的消息不能为空。" >&2
              return 1
            fi
            message="$2"
            shift 2
            ;;
          --message=*)
            if [[ -z "${1#--message=}" ]]; then
              echo "stash push 的消息不能为空。" >&2
              return 1
            fi
            message="${1#--message=}"
            shift
            ;;
          *)
            echo "stash push 仅支持可选的 -m/--message 参数，且不接受多余参数。" >&2
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
    *)
      echo "未知 stash 子命令: $subcommand" >&2
      return 1
      ;;
  esac
}

run_commit() {
  local message=""

  while (($#)); do
    case "$1" in
      -m|--message)
        if (($# < 2)) || [[ -z "$2" ]]; then
          echo "commit 需要通过 -m 或 --message 提供提交信息。" >&2
          return 1
        fi
        message="$2"
        shift 2
        ;;
      --message=*)
        if [[ -z "${1#--message=}" ]]; then
          echo "commit 需要通过 -m 或 --message 提供提交信息。" >&2
          return 1
        fi
        message="${1#--message=}"
        shift
        ;;
      *)
        echo "commit 仅支持 -m/--message，且不接受多余参数。" >&2
        return 1
        ;;
    esac
  done

  if [[ -z "$message" ]]; then
    echo "commit 需要通过 -m 或 --message 提供提交信息。" >&2
    return 1
  fi

  show_commit_preview
  perform_commit "$message"
}

run_switch() {
  if (($# < 1)) || [[ -z "${1:-}" ]]; then
    echo "switch 需要目标分支名。" >&2
    return 1
  fi
  if (($# > 1)); then
    echo "switch 只接受一个目标分支名。" >&2
    return 1
  fi

  local branch="$1"
  if ! git show-ref --verify --quiet "refs/heads/$branch"; then
    echo "本地分支不存在: $branch" >&2
    return 1
  fi

  git switch "$branch"
}

run_pull() {
  require_no_extra_args "pull" "$@"
  git pull --ff-only
}

run_push() {
  require_no_extra_args "push" "$@"
  git push
}

prompt_commit() {
  local message=""
  local confirmation=""
  if ! read -r -p "请输入提交信息: " message; then
    echo "读取输入失败。" >&2
    return 1
  fi
  if [[ -z "$message" ]]; then
    echo "commit 需要通过 -m 或 --message 提供提交信息。" >&2
    return 1
  fi

  echo "以下改动将被暂存并提交："
  show_commit_preview
  if ! read -r -p "输入 yes 确认执行 git add -A 并提交: " confirmation; then
    echo "读取输入失败。" >&2
    return 1
  fi
  if [[ "$confirmation" != "yes" ]]; then
    echo "已取消提交。"
    return 1
  fi

  perform_commit "$message"
}

prompt_switch() {
  local branch=""
  echo "本地分支列表:"
  git --no-pager branch --format='  %(refname:short)'
  if ! read -r -p "请输入目标分支名: " branch; then
    echo "读取输入失败。" >&2
    return 1
  fi
  run_switch "$branch"
}

prompt_stash() {
  local selection=""
  local message=""
  cat <<'EOF'
请选择 stash 操作:
  1) push
  2) pop
  3) list
EOF
  if ! read -r -p "请输入选项 [1-3]: " selection; then
    echo "读取输入失败。" >&2
    return 1
  fi

  case "$selection" in
    1|push)
      if ! read -r -p "请输入 stash 说明（可留空）: " message; then
        echo "读取输入失败。" >&2
        return 1
      fi
      if [[ -n "$message" ]]; then
        run_stash push -m "$message"
      else
        run_stash push
      fi
      ;;
    2|pop)
      run_stash pop
      ;;
    3|list)
      run_stash list
      ;;
    *)
      echo "无效的 stash 选项: $selection" >&2
      return 1
      ;;
  esac
}

show_menu() {
  local selection=""

  run_menu_action() {
    local action_label="$1"
    shift

    if "$@"; then
      return 0
    else
      local status=$?
      echo "菜单操作失败（$action_label，退出码: $status），返回菜单继续。" >&2
    fi
  }

  while true; do
    cat <<'EOF'
Git Helper 菜单:
  1) 查看状态
  2) 拉取更新
  3) 推送当前分支
  4) 暂存全部并提交
  5) 切换分支
  6) 查看日志
  7) Stash 操作
  8) 退出
EOF
    if ! read -r -p "请输入选项 [1-8]: " selection; then
      echo "读取输入失败。" >&2
      exit 1
    fi

    case "$selection" in
      1)
        run_menu_action "status" run_status
        ;;
      2)
        run_menu_action "pull" run_pull
        ;;
      3)
        run_menu_action "push" run_push
        ;;
      4)
        run_menu_action "commit" prompt_commit
        ;;
      5)
        run_menu_action "switch" prompt_switch
        ;;
      6)
        run_menu_action "log" run_log
        ;;
      7)
        run_menu_action "stash" prompt_stash
        ;;
      8)
        echo "退出菜单。"
        break
        ;;
      *)
        echo "无效选项: $selection" >&2
        ;;
    esac
  done
}

main() {
  local command="${1:-menu}"
  if (($# > 0)); then
    shift
  fi

  case "$command" in
    help|-h|--help)
      require_no_extra_args "help" "$@"
      usage
      ;;
    menu)
      require_no_extra_args "menu" "$@"
      show_menu
      ;;
    status)
      run_status "$@"
      ;;
    log)
      run_log "$@"
      ;;
    stash)
      run_stash "$@"
      ;;
    commit)
      run_commit "$@"
      ;;
    switch)
      run_switch "$@"
      ;;
    pull)
      run_pull "$@"
      ;;
    push)
      run_push "$@"
      ;;
    *)
      echo "未知命令: $command" >&2
      usage >&2
      exit 1
      ;;
  esac
}

cd "$repo_root"
ensure_git_repo
main "$@"
