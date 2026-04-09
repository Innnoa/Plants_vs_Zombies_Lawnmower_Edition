#!/usr/bin/env bash

set -euo pipefail

script_dir="$(cd "$(dirname "$0")" && pwd)"
repo_root="$(cd "$script_dir/.." && pwd)"

usage() {
  cat <<'EOF'
用法:
  script/git_helper.sh <命令> [参数]

命令:
  menu                 显示命令菜单
  status               显示 git status --short --branch
  pull                 预留命令（只读骨架，暂不执行）
  push                 预留命令（只读骨架，暂不执行）
  commit               校验提交参数（需 -m/--message）
  switch               校验分支参数（需目标分支名）
  log                  显示最近 15 条提交
  stash                仅支持: stash list
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
    exit 1
  fi
}

run_status() {
  require_no_extra_args "status" "$@"
  git status --short --branch
}

run_log() {
  require_no_extra_args "log" "$@"
  git log --oneline --decorate -n 15
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
              exit 1
            fi
            message="$2"
            shift 2
            ;;
          --message=*)
            if [[ -z "${1#--message=}" ]]; then
              echo "stash push 的消息不能为空。" >&2
              exit 1
            fi
            message="${1#--message=}"
            shift
            ;;
          *)
            echo "stash push 仅支持可选的 -m/--message 参数，且不接受多余参数。" >&2
            exit 1
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
      exit 1
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
          exit 1
        fi
        message="$2"
        shift 2
        ;;
      --message=*)
        if [[ -z "${1#--message=}" ]]; then
          echo "commit 需要通过 -m 或 --message 提供提交信息。" >&2
          exit 1
        fi
        message="${1#--message=}"
        shift
        ;;
      *)
        echo "commit 仅支持 -m/--message，且不接受多余参数。" >&2
        exit 1
        ;;
    esac
  done

  if [[ -z "$message" ]]; then
    echo "commit 需要通过 -m 或 --message 提供提交信息。" >&2
    exit 1
  fi

  git status --short
  git add -A
  git commit -m "$message"
}

run_switch() {
  if (($# < 1)) || [[ -z "${1:-}" ]]; then
    echo "switch 需要目标分支名。" >&2
    exit 1
  fi
  if (($# > 1)); then
    echo "switch 只接受一个目标分支名。" >&2
    exit 1
  fi

  local branch="$1"
  if ! git show-ref --verify --quiet "refs/heads/$branch"; then
    echo "本地分支不存在: $branch" >&2
    exit 1
  fi

  git switch "$branch"
}

run_pull() {
  require_no_extra_args "pull" "$@"
  echo "pull 命令在只读骨架阶段暂不执行。"
}

run_push() {
  require_no_extra_args "push" "$@"
  echo "push 命令在只读骨架阶段暂不执行。"
}

main() {
  local command="${1:-help}"
  if (($# > 0)); then
    shift
  fi

  case "$command" in
    help|-h|--help|menu)
      local help_command="help"
      if [[ "$command" == "menu" ]]; then
        help_command="menu"
      fi
      require_no_extra_args "$help_command" "$@"
      usage
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
