#!/usr/bin/env bash
# Quick network latency check for LawnMowerGame sessions.
# Usage: bash script/network_latency_check.sh <host> [--count 20] [--interval 0.2] [--deadline 10]

set -euo pipefail

usage() {
  cat <<'EOF'
用法:
  script/network_latency_check.sh <目标主机> [选项]

选项:
  -c, --count N       发送多少次 ICMP 请求 (默认: 20)
  -i, --interval 秒   每次请求间隔时间 (默认: 0.2)
  -w, --deadline 秒   ping 命令整体超时时间 (默认: 10)
  -h, --help          显示本帮助

示例:
  script/network_latency_check.sh game.example.com --count 30 --interval 0.1
EOF
}

if ! command -v ping >/dev/null 2>&1; then
  echo "未找到 ping 命令，请安装后重试（例如 iputils-ping）。" >&2
  exit 1
fi

host=""
count=20
interval=0.2
deadline=10

while (($#)); do
  case "$1" in
    -c|--count)
      count="$2"
      shift 2
      ;;
    -i|--interval)
      interval="$2"
      shift 2
      ;;
    -w|--deadline)
      deadline="$2"
      shift 2
      ;;
    -h|--help)
      usage
      exit 0
      ;;
    --)
      shift
      break
      ;;
    *)
      if [[ -z "$host" ]]; then
        host="$1"
        shift
      else
        echo "未知参数: $1" >&2
        usage
        exit 1
      fi
      ;;
  esac
done

if [[ -z "$host" ]]; then
  echo "必须指定目标主机。" >&2
  usage
  exit 1
fi

ge() {
  # Compare two floats: returns 0 if $1 >= $2.
  awk -v a="$1" -v b="$2" 'BEGIN {exit !(a>=b)}'
}

tmp_out="$(mktemp)"
trap 'rm -f "$tmp_out"' EXIT

echo "Pinging $host (${count}x, interval ${interval}s, deadline ${deadline}s)..."
if ! ping -n -c "$count" -i "$interval" -w "$deadline" "$host" >"$tmp_out" 2>&1; then
  echo "ping 执行失败:" >&2
  cat "$tmp_out" >&2
  if grep -qi "Operation not permitted" "$tmp_out"; then
    echo "提示: 可能缺少原始套接字权限。尝试使用 sudo 运行，或为 ping 授权 cap_net_raw。"
  fi
  exit 1
fi

packets_line="$(grep -E 'packets transmitted' "$tmp_out" | tail -n1 || true)"
rtt_line="$(grep -E 'round-trip|rtt' "$tmp_out" | tail -n1 || true)"

if [[ -z "$packets_line" || -z "$rtt_line" ]]; then
  echo "无法解析 ping 输出:" >&2
  cat "$tmp_out" >&2
  exit 1
fi

transmitted="$(echo "$packets_line" | awk -F',' '{print $1}' | awk '{print $1}')"
received="$(echo "$packets_line" | awk -F',' '{print $2}' | awk '{print $1}')"
loss_pct="$(echo "$packets_line" | awk -F',' '{for (i=1;i<=NF;i++) if ($i ~ /packet loss/) {gsub(/[^0-9.]/,"",$i); print $i}}')"

rtt_values="$(echo "$rtt_line" | awk -F' = ' '{print $2}')"
rtt_values_clean="$(echo "$rtt_values" | awk '{gsub(/ ms/,""); print $0}')"
IFS='/' read -r rtt_min rtt_avg rtt_max rtt_jitter <<< "$rtt_values_clean"

if [[ -z "$loss_pct" || -z "$rtt_avg" ]]; then
  echo "缺少解析到的指标（丢包或平均时延）。" >&2
  cat "$tmp_out" >&2
  exit 1
fi

printf "包统计: 发送 %s, 接收 %s, 丢包率 %.1f%%\n" "$transmitted" "$received" "$loss_pct"
printf "往返延迟 (ms): 最小 %.2f | 平均 %.2f | 最大 %.2f | 抖动 %.2f\n" "$rtt_min" "$rtt_avg" "$rtt_max" "$rtt_jitter"

rating=""
reason=""

if ge "$loss_pct" 20; then
  rating="很差"
  reason="丢包率 >=20%，联机会严重卡顿/掉线"
elif ge "$loss_pct" 5; then
  rating="一般"
  reason="丢包率偏高，可能出现瞬时卡顿"
elif ge "$rtt_avg" 160; then
  rating="较差"
  reason="平均延迟 >=160ms，联机体验可能明显滞后"
elif ge "$rtt_avg" 120; then
  rating="可用"
  reason="平均延迟 120-160ms，可能偶发延迟"
elif ge "$rtt_avg" 80; then
  rating="良好"
  reason="平均延迟 80-120ms，可正常联机"
else
  rating="优秀"
  reason="平均延迟 <80ms，联机流畅"
fi

if ge "$rtt_jitter" 25; then
  reason+="；抖动 ${rtt_jitter}ms 偏大，延迟可能不稳定"
fi

echo "评估: $rating —— $reason"
