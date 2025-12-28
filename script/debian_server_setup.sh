#!/usr/bin/env bash
# Debian server setup for LawnMower Game (server only).
# Usage: bash script/debian_server_setup.sh

set -euo pipefail

run_sudo() {
  if command -v sudo >/dev/null 2>&1; then
    sudo "$@"
  else
    "$@"
  fi
}

echo "[1/1] Installing system dependencies..."
run_sudo apt-get update
run_sudo apt-get install -y \
  build-essential cmake pkg-config \
  libprotobuf-dev protobuf-compiler \
  libspdlog-dev libabsl-dev libasio-dev
echo "Dependencies installed. Build manually when needed, e.g.:"
echo "  cmake -S server -B server/build -DCMAKE_BUILD_TYPE=Release"
echo "  cmake --build server/build -j\"\$(nproc)\""
