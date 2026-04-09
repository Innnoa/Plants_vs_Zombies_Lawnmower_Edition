# 2026-04-09 P2-3 Cross-Tick State Cache Execution Plan

## Internal Grade

`XL`

说明：

- 这是服务器侧多步骤优化，但当前没有用户明确的多代理授权，所以采用 `XL` 的波次化串行执行，不使用 `spawn_agent`。

## Waves

### Wave 1

冻结范围：只给状态 backlog 包做跨 tick prepared bytes 复用，并允许 backlog 包冻结首次入队 timing。

### Wave 2

先写失败测试：

- `state_sync_backlog_tcp_identity`
- `state_sync_backlog_udp_identity`
- `state_sync_backlog_frozen_timing`

### Wave 3

扩展 `PreparedStatePacket`，增加 backlog 持久 bytes 与 frozen timing 字段。

### Wave 4

修改 backlog 入队逻辑：首次进入 backlog 时冻结 timing，并预构所需 TCP/UDP bytes。

### Wave 5

修改 backlog drain 逻辑：若 packet 已冻结并持有 prepared bytes，则直接复用，不再刷新 timing。

### Wave 6

构建、跑单测与聚焦回归、写 cleanup receipt。

## Verification Commands

```bash
env CCACHE_DISABLE=1 script/build_server.sh --debug
ctest --test-dir server/build-debug --output-on-failure -R "state_sync_backlog|dispatch_cache|event_dispatch_backlog|channel_policy|server_smoke|udp_sync_smoke|tcp_event_batch_smoke|state_packet_budget_smoke|delta_packet_budget_smoke|event_backpressure_smoke|room_sync_budget_smoke|full_snapshot_chunking_smoke"
```

## Delivery Acceptance

- 只有在新增测试先失败后转绿，且状态 / 预算 / 分块相关回归仍通过时，才允许声称本阶段完成。

## Rollback Rules

- 若实现过程中发现必须修改客户端状态时间语义或协议字段，立即停止并回到设计层重审。
