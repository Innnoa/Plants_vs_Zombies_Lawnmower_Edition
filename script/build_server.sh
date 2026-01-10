#!/usr/bin/env bash
# Build LawnMowerGame server with ccache configured under ~/.cache/ccache.
#
# Why:
#   Some environments may have a non-writable default ccache temp dir (e.g. /run/user/...),
#   leading to "ccache: failed to create temporary file ... Permission denied".
#   This script pins CCACHE_DIR/CCACHE_TEMPDIR/TMPDIR to a writable home cache directory.
#
# Usage:
#   script/build_server.sh [--debug|--release] [-B <build-dir>] [-j <jobs>]
#   script/build_server.sh --build-only [-B <build-dir>] [-j <jobs>]
#   script/build_server.sh --configure-only [--debug|--release] [-B <build-dir>]

set -euo pipefail

usage() {
  cat <<'EOF'
用法:
  script/build_server.sh [选项]

选项:
  --debug              Debug 构建 (默认)
  --release            Release 构建
  -B, --build-dir DIR  指定构建目录 (默认: server/build-debug 或 server/build-release)
  -j, --jobs N         并行编译数量 (默认: nproc)
  --configure-only     只执行 cmake 配置，不编译
  --build-only         只编译，不重新配置
  -h, --help           显示帮助

说明:
  本脚本会设置:
    CCACHE_DIR=$HOME/.cache/ccache
    CCACHE_TEMPDIR=$HOME/.cache/ccache/tmp
    TMPDIR=$HOME/.cache/ccache/tmp
  用于避免 ccache 默认临时目录不可写的问题。

  如果系统没有启用 ccache，本脚本会在可用时自动添加:
    -DCMAKE_C_COMPILER_LAUNCHER=ccache
    -DCMAKE_CXX_COMPILER_LAUNCHER=ccache
EOF
}

script_dir="$(cd "$(dirname "$0")" && pwd)"
repo_root="$(cd "$script_dir/.." && pwd)"
server_dir="$repo_root/server"

build_type="Debug"
build_dir=""
jobs=""
do_configure=1
do_build=1

while (($#)); do
  case "$1" in
    --debug)
      build_type="Debug"
      shift
      ;;
    --release)
      build_type="Release"
      shift
      ;;
    -B|--build-dir)
      build_dir="$2"
      shift 2
      ;;
    -j|--jobs)
      jobs="$2"
      shift 2
      ;;
    --configure-only)
      do_build=0
      shift
      ;;
    --build-only)
      do_configure=0
      shift
      ;;
    -h|--help)
      usage
      exit 0
      ;;
    *)
      echo "未知参数: $1" >&2
      usage >&2
      exit 1
      ;;
  esac
done

if [[ -z "$build_dir" ]]; then
  if [[ "$build_type" == "Release" ]]; then
    build_dir="$server_dir/build-release"
  else
    build_dir="$server_dir/build-debug"
  fi
fi

cache_dir="${CCACHE_DIR:-$HOME/.cache/ccache}"
cache_tmp="${CCACHE_TEMPDIR:-$cache_dir/tmp}"
if ! mkdir -p "$cache_dir" "$cache_tmp"; then
  echo "无法创建 ccache 目录: $cache_dir / $cache_tmp" >&2
  echo "请检查权限，或改用自定义路径，例如:" >&2
  echo "  CCACHE_DIR=/path/to/ccache CCACHE_TEMPDIR=/path/to/ccache/tmp script/build_server.sh" >&2
  exit 1
fi

export CCACHE_DIR="$cache_dir"
export CCACHE_TEMPDIR="$cache_tmp"
export TMPDIR="$cache_tmp"

if (( do_configure )); then
  cmake_args=("-DCMAKE_BUILD_TYPE=$build_type")
  if command -v ccache >/dev/null 2>&1; then
    cmake_args+=("-DCMAKE_C_COMPILER_LAUNCHER=ccache")
    cmake_args+=("-DCMAKE_CXX_COMPILER_LAUNCHER=ccache")
  fi

  cmake -S "$server_dir" -B "$build_dir" "${cmake_args[@]}"
fi

if (( do_build )); then
  if [[ -z "$jobs" ]]; then
    if command -v nproc >/dev/null 2>&1; then
      jobs="$(nproc)"
    else
      jobs="4"
    fi
  fi
  cmake --build "$build_dir" -j "$jobs"
fi

echo "Build done:"
echo "  build_dir=$build_dir"
echo "  CCACHE_DIR=$CCACHE_DIR"
echo "  CCACHE_TEMPDIR=$CCACHE_TEMPDIR"
