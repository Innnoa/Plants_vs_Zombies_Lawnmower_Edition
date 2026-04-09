# 2026-04-09 P2-3 Cross-Tick Event Cache Execution Plan

## Internal Grade

`XL`

说明：

- 这是服务器侧多步骤优化，但当前没有用户明确的多代理授权，所以采用 `XL` 的波次化串行执行，不使用 `spawn_agent`。

## Waves

### Wave 1

冻结第二阶段范围：只做延迟非关键事件 backlog 的跨 tick prepared bytes 复用。

### Wave 2

先写失败测试：

- backlog packet 跨 tick 复用 identity 测试
- backlog drain 后房间记录清理测试

### Wave 3

扩展 event backlog packet 模型，允许 backlog packet 持有持久 framed bytes。

### Wave 4

把 backlog drain 发送路径改为优先复用 packet 自带 prepared bytes。

### Wave 5

补齐 `game_over` / 房间删除 / 显式 clear 的清理行为，并跑聚焦回归。

## Verification Commands

```bash
env CCACHE_DISABLE=1 script/build_server.sh --debug
ctest --test-dir server/build-debug --output-on-failure -R "dispatch_cache|event_dispatch_backlog|channel_policy|server_smoke|udp_sync_smoke|tcp_event_batch_smoke|state_packet_budget_smoke|delta_packet_budget_smoke|event_backpressure_smoke|room_sync_budget_smoke|full_snapshot_chunking_smoke"
```

## Delivery Acceptance

- 只有在新增测试先失败后转绿，且事件 / 预算 / 分块相关回归仍通过时，才允许声称第二阶段完成。

## Rollback Rules

- 若实现过程中发现必须改状态同步 timing 或协议字段，立即停止并回到设计层重审。
