#!/usr/bin/env bash
# Rsync sync helper for LawnMowerGame. Usage:
#   RSYNC_DEST=user@host:/home/user/LawnMowerGame ./script/rsync_sync.sh
# Optional: RSYNC_SSH_KEY=~/.ssh/id_ed25519

set -euo pipefail

repo_root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
dest="${RSYNC_DEST:-}"
ssh_key="${RSYNC_SSH_KEY:-}"

if [[ -z "$dest" ]]; then
  echo "RSYNC_DEST is required, e.g. user@host:/home/user/LawnMowerGame" >&2
  exit 1
fi

ssh_opt=()
if [[ -n "$ssh_key" ]]; then
  ssh_opt=(-e "ssh -i $ssh_key")
fi

excludes=(
  --exclude .git
  --exclude .idea
  --exclude .vscode
  --exclude '*.log'
  --exclude 'server/build*'
  --exclude 'server/.cache'
  --exclude 'client/**/build'
  --exclude 'client/**/.gradle'
  --exclude 'client/.gradle'
  --exclude '**/.DS_Store'
)

echo "Syncing from $repo_root/ to $dest"
rsync -avz --delete --info=progress2 --partial --append-verify \
  "${excludes[@]}" \
  "${ssh_opt[@]}" \
  "$repo_root/" "$dest/"
