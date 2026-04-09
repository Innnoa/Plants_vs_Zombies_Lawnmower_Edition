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

assert_equals() {
  local actual="$1"
  local expected="$2"
  if [[ "$actual" != "$expected" ]]; then
    fail "值不符合预期，actual=[$actual], expected=[$expected]"
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
git -C "$tmp_repo" add README.md script/git_helper.sh
git -C "$tmp_repo" commit -m "initial commit" >/dev/null
base_branch="$(git -C "$tmp_repo" symbolic-ref --short HEAD)"

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

git -C "$tmp_repo" branch feature/demo

run_helper switch feature/demo >/dev/null
current_branch="$(git -C "$tmp_repo" symbolic-ref --short HEAD)"
assert_equals "$current_branch" "feature/demo"

run_helper switch "$base_branch" >/dev/null
current_branch="$(git -C "$tmp_repo" symbolic-ref --short HEAD)"
assert_equals "$current_branch" "$base_branch"

echo "tracked change" >> "$tmp_repo/README.md"
echo "new file" > "$tmp_repo/NEW_FILE.txt"
run_helper commit -m "feat: helper commit" >/dev/null
latest_message="$(git -C "$tmp_repo" log -1 --pretty=%s)"
assert_equals "$latest_message" "feat: helper commit"
commit_files="$(git -C "$tmp_repo" show --name-only --pretty='' HEAD)"
assert_contains "$commit_files" "README.md"
assert_contains "$commit_files" "NEW_FILE.txt"

run_helper switch feature/demo >/dev/null
current_branch="$(git -C "$tmp_repo" symbolic-ref --short HEAD)"
assert_equals "$current_branch" "feature/demo"

echo "stash line" >> "$tmp_repo/README.md"
run_helper stash push -m "wip: test stash" >/dev/null
stash_after_push="$(git -C "$tmp_repo" stash list)"
assert_contains "$stash_after_push" "wip: test stash"
if grep -q "stash line" "$tmp_repo/README.md"; then
  fail "stash push 后工作区应回滚改动"
fi

run_helper stash pop >/dev/null
if ! grep -q "stash line" "$tmp_repo/README.md"; then
  fail "stash pop 后应恢复工作区内容"
fi

assert_command_fails "commit 需要通过 -m 或 --message 提供提交信息。" run_helper commit
assert_command_fails "status 不接受额外参数。" run_helper status extra
assert_command_fails "log 不接受额外参数。" run_helper log extra
assert_command_fails "pull 不接受额外参数。" run_helper pull extra
assert_command_fails "push 不接受额外参数。" run_helper push extra
assert_command_fails "help 不接受额外参数。" run_helper --help extra
assert_command_fails "stash list 不接受额外参数。" run_helper stash list extra
assert_command_fails "switch 只接受一个目标分支名。" run_helper switch main extra
assert_command_fails "commit 仅支持 -m/--message，且不接受多余参数。" run_helper commit -m msg extra
assert_command_fails "commit 仅支持 -m/--message，且不接受多余参数。" run_helper commit --message msg --amend
assert_command_fails "switch 需要目标分支名。" run_helper switch

echo "git_helper read-only smoke test: PASS"
