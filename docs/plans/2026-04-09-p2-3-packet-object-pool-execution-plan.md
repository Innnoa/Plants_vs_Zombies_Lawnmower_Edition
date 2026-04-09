# 2026-04-09 P2-3 Packet Object Pool Execution Plan

## Internal Grade

`XL`

说明：

- 这是服务器侧多步骤优化，但当前没有用户明确的多代理授权，所以采用 `XL` 的波次化串行执行，不使用 `spawn_agent`。

## Waves

### Wave 1

冻结范围：只给发送热路径里的 `lawnmower::Packet` 做轻量对象池。

### Wave 2

先写失败测试：

- `packet_object_pool_reuses_address`
- `packet_object_pool_clears_fields`

### Wave 3

新增 `network/shared/packet_object_pool.hpp`，提供 acquire / release 的 RAII 句柄。

### Wave 4

把 `tcp_session.cpp`、`udp_server.cpp`、`game_manager_dispatch_cache.cpp`、`game_manager_sync_dispatch.cpp` 的构包热点接到对象池。

### Wave 5

构建、跑单测与聚焦回归、写 cleanup receipt。

## Verification Commands

```bash
env CCACHE_DISABLE=1 script/build_server.sh --debug
ctest --test-dir server/build-debug --output-on-failure -R "packet_object_pool|state_sync_backlog|dispatch_cache|event_dispatch_backlog|channel_policy|server_smoke|udp_sync_smoke|tcp_event_batch_smoke|state_packet_budget_smoke|delta_packet_budget_smoke|event_backpressure_smoke|room_sync_budget_smoke|full_snapshot_chunking_smoke"
```

## Delivery Acceptance

- 只有在新增测试先失败后转绿，且发送/预算/分块相关回归仍通过时，才允许声称本阶段完成。

## Rollback Rules

- 若实现过程中发现需要把对象池扩展到业务 message 或接收链路，立即停止并回到设计层重审。
