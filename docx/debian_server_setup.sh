#!/usr/bin/env bash
# Debian server setup for LawnMower Game (server only).
# Usage: bash docx/debian_server_setup.sh

set -euo pipefail

repo_root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"

run_sudo() {
  if command -v sudo >/dev/null 2>&1; then
    sudo "$@"
  else
    "$@"
  fi
}

echo "[1/3] Installing system dependencies..."
run_sudo apt-get update
run_sudo apt-get install -y \
  build-essential cmake pkg-config \
  libprotobuf-dev protobuf-compiler \
  libspdlog-dev libabsl-dev libasio-dev

echo "[2/3] Configuring CMake build..."
build_dir="$repo_root/server/build"
cmake -S "$repo_root/server" -B "$build_dir" -DCMAKE_BUILD_TYPE=Release

echo "[3/3] Building server..."
cmake --build "$build_dir" -j"$(nproc)"

echo "Done. Binary located at: $build_dir/server"
echo "Run with (example): $build_dir/server --config \"$repo_root/server/config/server_config.json\""
