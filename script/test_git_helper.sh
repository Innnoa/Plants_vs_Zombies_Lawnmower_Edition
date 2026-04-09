#!/usr/bin/env bash

set -euo pipefail

script_dir="$(cd "$(dirname "$0")" && pwd)"
repo_root="$(cd "$script_dir/.." && pwd)"

fail() {
  echo "FAIL: $*" >&2
  exit 1
}

assert_contains() {
  local haystack="$1"
  local needle="$2"
  if [[ "$haystack" != *"$needle"* ]]; then
    fail "输出未包含预期内容: $needle"
  fi
}

assert_command_fails() {
  local expected_message="$1"
  shift

  local output=""
  set +e
  output="$("$@" 2>&1)"
  local status=$?
  set -e

  if [[ $status -eq 0 ]]; then
    fail "命令应失败但返回成功: $*"
  fi

  assert_contains "$output" "$expected_message"
}

tmp_dir="$(mktemp -d)"
trap 'rm -rf "$tmp_dir"' EXIT

tmp_repo="$tmp_dir/repo"
mkdir -p "$tmp_repo/script"

cp "$repo_root/script/git_helper.sh" "$tmp_repo/script/git_helper.sh"
chmod +x "$tmp_repo/script/git_helper.sh"

git -C "$tmp_repo" init
git -C "$tmp_repo" config user.name "Smoke Test"
git -C "$tmp_repo" config user.email "smoke@example.com"

echo "seed" > "$tmp_repo/README.md"
git -C "$tmp_repo" add README.md
git -C "$tmp_repo" commit -m "initial commit" >/dev/null

run_helper() {
  (
    cd "$tmp_repo"
    "$tmp_repo/script/git_helper.sh" "$@"
  )
}

help_output="$(run_helper --help)"
assert_contains "$help_output" "用法:"

status_output="$(run_helper status)"
assert_contains "$status_output" "## "

log_output="$(run_helper log)"
assert_contains "$log_output" "initial commit"

stash_output="$(run_helper stash list)"
if [[ -n "$stash_output" ]]; then
  fail "空仓库 stash list 结果应为空"
fi

assert_command_fails "commit 需要通过 -m 或 --message 提供提交信息。" run_helper commit
assert_command_fails "switch 需要目标分支名。" run_helper switch

echo "git_helper read-only smoke test: PASS"
