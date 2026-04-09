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

install_helper() {
  local repo_path="$1"
  mkdir -p "$repo_path/script"
  cp "$repo_root/script/git_helper.sh" "$repo_path/script/git_helper.sh"
  chmod +x "$repo_path/script/git_helper.sh"
}

install_helper "$tmp_repo"

git -C "$tmp_repo" init
git -C "$tmp_repo" config user.name "Smoke Test"
git -C "$tmp_repo" config user.email "smoke@example.com"

echo "seed" > "$tmp_repo/README.md"
git -C "$tmp_repo" add README.md script/git_helper.sh
git -C "$tmp_repo" commit -m "initial commit" >/dev/null
base_branch="$(git -C "$tmp_repo" symbolic-ref --short HEAD)"

run_helper() {
  run_helper_in_repo "$tmp_repo" "$@"
}

run_helper_in_repo() {
  local repo_path="$1"
  shift
  (
    cd "$repo_path"
    "$repo_path/script/git_helper.sh" "$@"
  )
}

help_output="$(run_helper --help)"
assert_contains "$help_output" "用法:"
assert_contains "$help_output" "不带参数时默认进入 menu"

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
assert_command_fails "switch 只接受一个目标分支名。" run_helper switch "$base_branch" extra
assert_command_fails "commit 仅支持 -m/--message，且不接受多余参数。" run_helper commit -m msg extra
assert_command_fails "commit 仅支持 -m/--message，且不接受多余参数。" run_helper commit --message msg --amend
assert_command_fails "switch 需要目标分支名。" run_helper switch

remote_repo="$tmp_dir/remote.git"
alice_repo="$tmp_dir/alice"
bob_repo="$tmp_dir/bob"

git init --bare "$remote_repo" >/dev/null
git clone "$remote_repo" "$alice_repo" >/dev/null
git clone "$remote_repo" "$bob_repo" >/dev/null

install_helper "$alice_repo"
install_helper "$bob_repo"

git -C "$alice_repo" config user.name "Alice"
git -C "$alice_repo" config user.email "alice@example.com"
git -C "$bob_repo" config user.name "Bob"
git -C "$bob_repo" config user.email "bob@example.com"

echo "alice seed" > "$alice_repo/REMOTE.md"
git -C "$alice_repo" add REMOTE.md
git -C "$alice_repo" commit -m "chore: init remote" >/dev/null
remote_base_branch="$(git -C "$alice_repo" symbolic-ref --short HEAD)"
git -C "$alice_repo" push -u origin "$remote_base_branch" >/dev/null

git -C "$bob_repo" fetch origin "$remote_base_branch" >/dev/null
git -C "$bob_repo" switch -c "$remote_base_branch" --track "origin/$remote_base_branch" >/dev/null

echo "bob update" >> "$bob_repo/REMOTE.md"
git -C "$bob_repo" add REMOTE.md
git -C "$bob_repo" commit -m "feat: bob update" >/dev/null
run_helper_in_repo "$bob_repo" push >/dev/null

bob_head_after_push="$(git -C "$bob_repo" rev-parse HEAD)"
run_helper_in_repo "$alice_repo" pull >/dev/null
alice_head_after_pull="$(git -C "$alice_repo" rev-parse HEAD)"
assert_equals "$alice_head_after_pull" "$bob_head_after_push"

echo "alice followup" >> "$alice_repo/REMOTE.md"
git -C "$alice_repo" add REMOTE.md
git -C "$alice_repo" commit -m "feat: alice followup" >/dev/null
git -C "$alice_repo" push >/dev/null

alice_head_after_push="$(git -C "$alice_repo" rev-parse HEAD)"
run_helper_in_repo "$bob_repo" pull >/dev/null
bob_head_after_pull="$(git -C "$bob_repo" rev-parse HEAD)"
assert_equals "$bob_head_after_pull" "$alice_head_after_push"

menu_fail_then_exit_output="$(printf '7\n2\n8\n' | run_helper_in_repo "$alice_repo" menu 2>&1)"
assert_contains "$menu_fail_then_exit_output" "菜单操作失败（stash"
assert_contains "$menu_fail_then_exit_output" "退出菜单"

menu_output="$(printf '8\n' | run_helper_in_repo "$alice_repo" menu)"
assert_contains "$menu_output" "退出"

echo "git_helper smoke test: PASS"
