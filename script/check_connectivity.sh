#!/usr/bin/env bash

set -euo pipefail

usage() {
  cat <<'EOF'
用法:
  script/check_connectivity.sh [选项]

选项:
  --host HOST        客户端连接地址（默认: 127.0.0.1）
  --tcp-port PORT    服务端 TCP 端口（默认: 自动选择空闲端口）
  --udp-port PORT    服务端 UDP 端口（默认: 自动选择空闲端口）
  --skip-build       跳过服务端构建
  --skip-smoke       跳过服务端 smoke tests
  --run-ping         额外执行 ping 延迟检查
  -h, --help         显示帮助

环境变量:
  LAWNMOWER_SERVER_HOST
  LAWNMOWER_SERVER_TCP_PORT
  LAWNMOWER_SERVER_UDP_PORT
  LAWNMOWER_CONNECTIVITY_TIMEOUT_MS
  LAWNMOWER_SERVER_BUILD_DIR

说明:
  该脚本会：
    1. 构建服务端
    2. 运行服务端 server_smoke / udp_sync_smoke
    3. 启动一份本地服务端实例（默认自动选择空闲端口）
    4. 执行客户端真实 TCP/UDP 联通诊断
EOF
}

script_dir="$(cd "$(dirname "$0")" && pwd)"
repo_root="$(cd "$script_dir/.." && pwd)"
tmp_root="${TMPDIR:-/tmp}"
gradle_wrapper_timeout_ms="${LAWNMOWER_GRADLE_WRAPPER_TIMEOUT_MS:-120000}"
local_gradle_bin_default="$tmp_root/gradle-8.12/bin/gradle"

if [[ -z "${CCACHE_DIR:-}" ]]; then
  if mkdir -p "$HOME/.cache/ccache/tmp" >/dev/null 2>&1; then
    export CCACHE_DIR="$HOME/.cache/ccache"
  else
    export CCACHE_DIR="$tmp_root/lawnmower-ccache"
  fi
else
  export CCACHE_DIR
fi

if [[ -z "${CCACHE_TEMPDIR:-}" ]]; then
  export CCACHE_TEMPDIR="$CCACHE_DIR/tmp"
else
  export CCACHE_TEMPDIR
fi

if [[ -z "${GRADLE_USER_HOME:-}" ]]; then
  if mkdir -p "$HOME/.gradle" >/dev/null 2>&1; then
    export GRADLE_USER_HOME="$HOME/.gradle"
  else
    export GRADLE_USER_HOME="$tmp_root/lawnmower-gradle"
  fi
else
  export GRADLE_USER_HOME
fi

if [[ "${GRADLE_OPTS:-}" != *"org.gradle.wrapper.networkTimeout"* ]]; then
  export GRADLE_OPTS="${GRADLE_OPTS:-} -Dorg.gradle.wrapper.networkTimeout=${gradle_wrapper_timeout_ms}"
fi
if [[ -n "${LAWNMOWER_GRADLE_BIN:-}" ]]; then
  gradle_bin="$LAWNMOWER_GRADLE_BIN"
elif [[ -x "$local_gradle_bin_default" ]]; then
  gradle_bin="$local_gradle_bin_default"
else
  gradle_bin=""
fi
build_dir="${LAWNMOWER_SERVER_BUILD_DIR:-$repo_root/server/build-debug}"
host="${LAWNMOWER_SERVER_HOST:-127.0.0.1}"
tcp_port="${LAWNMOWER_SERVER_TCP_PORT:-}"
udp_port="${LAWNMOWER_SERVER_UDP_PORT:-}"
tcp_port_explicit=0
udp_port_explicit=0
skip_build=0
skip_smoke=0
run_ping=0
workspace_dir=""
server_pid=""

while (($#)); do
  case "$1" in
    --host)
      host="$2"
      shift 2
      ;;
    --tcp-port)
      tcp_port="$2"
      tcp_port_explicit=1
      shift 2
      ;;
    --udp-port)
      udp_port="$2"
      udp_port_explicit=1
      shift 2
      ;;
    --skip-build)
      skip_build=1
      shift
      ;;
    --skip-smoke)
      skip_smoke=1
      shift
      ;;
    --run-ping)
      run_ping=1
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

cleanup() {
  if [[ -n "$server_pid" ]] && kill -0 "$server_pid" >/dev/null 2>&1; then
    kill "$server_pid" >/dev/null 2>&1 || true
    wait "$server_pid" >/dev/null 2>&1 || true
  fi
  if [[ -n "$workspace_dir" ]] && [[ -d "$workspace_dir" ]]; then
    rm -rf "$workspace_dir"
  fi
}

run_gradle() {
  if [[ -n "$gradle_bin" ]]; then
    "$gradle_bin" --no-daemon "$@"
  else
    bash ./gradlew --no-daemon "$@"
  fi
}

is_port_busy() {
  local proto="$1"
  local port="$2"
  if [[ "$proto" == "tcp" ]]; then
    ss -ltn "sport = :$port" 2>/dev/null | tail -n +2 | grep -q .
  else
    ss -lun "sport = :$port" 2>/dev/null | tail -n +2 | grep -q .
  fi
}

pick_free_port() {
  local proto="$1"
  local candidate=""
  local attempt=0
  while (( attempt < 200 )); do
    candidate="$(shuf -i 20000-45000 -n 1)"
    if ! is_port_busy "$proto" "$candidate"; then
      echo "$candidate"
      return 0
    fi
    attempt=$((attempt + 1))
  done
  echo "无法为 $proto 自动找到空闲端口" >&2
  exit 1
}

choose_ports_if_needed() {
  if [[ -z "$tcp_port" ]]; then
    tcp_port="$(pick_free_port tcp)"
  fi
  if [[ -z "$udp_port" ]]; then
    while true; do
      udp_port="$(pick_free_port udp)"
      if [[ "$udp_port" != "$tcp_port" ]]; then
        break
      fi
    done
  fi
}

wait_for_local_server() {
  local deadline=$((SECONDS + 10))
  while ((SECONDS < deadline)); do
    if bash -lc "exec 3<>/dev/tcp/127.0.0.1/$tcp_port" >/dev/null 2>&1; then
      return 0
    fi
    sleep 0.2
  done
  return 1
}

prepare_workspace() {
  workspace_dir="$(mktemp -d "${TMPDIR:-/tmp}/lawnmower-connectivity-XXXXXX")"
  mkdir -p "$workspace_dir/game_config"
  cp "$repo_root"/game_config/*.json "$workspace_dir/game_config/"

  sed -i -E \
    "s/(\"tcp_port\"[[:space:]]*:[[:space:]]*)[0-9]+/\1${tcp_port}/" \
    "$workspace_dir/game_config/server_config.json"
  sed -i -E \
    "s/(\"udp_port\"[[:space:]]*:[[:space:]]*)[0-9]+/\1${udp_port}/" \
    "$workspace_dir/game_config/server_config.json"
  sed -i -E \
    's/("log_level"[[:space:]]*:[[:space:]]*)".*"/\1"warn"/' \
    "$workspace_dir/game_config/server_config.json"
}

trap cleanup EXIT

if [[ -n "${LAWNMOWER_SERVER_TCP_PORT:-}" ]]; then
  tcp_port_explicit=1
fi
if [[ -n "${LAWNMOWER_SERVER_UDP_PORT:-}" ]]; then
  udp_port_explicit=1
fi

choose_ports_if_needed

if (( tcp_port_explicit == 1 )) && is_port_busy tcp "$tcp_port"; then
  echo "指定的 TCP 端口已被占用: $tcp_port" >&2
  exit 1
fi
if (( udp_port_explicit == 1 )) && is_port_busy udp "$udp_port"; then
  echo "指定的 UDP 端口已被占用: $udp_port" >&2
  exit 1
fi

echo "[1/4] 构建服务端"
if (( skip_build == 0 )); then
  "$repo_root/script/build_server.sh" --debug
else
  echo "跳过服务端构建"
fi

server_bin="$build_dir/server"
if [[ ! -x "$server_bin" ]]; then
  echo "未找到服务端可执行文件: $server_bin" >&2
  exit 1
fi

echo "[2/4] 运行服务端 smoke tests"
if (( skip_smoke == 0 )); then
  ctest --test-dir "$build_dir" --output-on-failure -R 'server_smoke|udp_sync_smoke'
else
  echo "跳过服务端 smoke tests"
fi

echo "[3/4] 启动本地服务端实例"
prepare_workspace
echo "本次联通检查端口: tcp=$tcp_port udp=$udp_port"
(
  cd "$workspace_dir"
  "$server_bin" >"$workspace_dir/server.log" 2>&1
) &
server_pid=$!

if ! wait_for_local_server; then
  echo "服务端在预期时间内未监听 TCP 端口 $tcp_port" >&2
  if [[ -f "$workspace_dir/server.log" ]]; then
    tail -n 40 "$workspace_dir/server.log" >&2
  fi
  exit 1
fi

if (( run_ping == 1 )); then
  echo "[附加] 执行基础延迟检查"
  "$repo_root/script/network_latency_check.sh" "$host"
fi

echo "[4/4] 运行客户端真实链路诊断"
(
  cd "$repo_root/client"
  export LAWNMOWER_SERVER_HOST="$host"
  export LAWNMOWER_SERVER_TCP_PORT="$tcp_port"
  export LAWNMOWER_SERVER_UDP_PORT="$udp_port"
  run_gradle :core:classes >/dev/null
  connectivity_classpath="$(run_gradle -q :core:printConnectivityCheckClasspath | tail -n 1)"
  java -cp "$connectivity_classpath" com.lawnmower.network.ConnectivityCheck
)

echo "联通检查完成。"
