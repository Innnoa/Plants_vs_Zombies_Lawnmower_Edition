# 2026-04-09 P2-3 Full Snapshot Backlog Template Execution Plan

## Internal Grade

`XL`

说明：

- 这是服务器侧多步骤优化，但当前没有用户明确的多代理授权，所以采用 `XL` 的波次化串行执行，不使用 `spawn_agent`。

## Waves

### Wave 1

冻结范围：只给 full snapshot backlog chunk 做跨 tick 模板复用，发送时保持实际发送 tick 语义。

### Wave 2

先写失败测试：

- `full_snapshot_backlog_tick_updates`
- `full_snapshot_backlog_template_reuse`

### Wave 3

扩展 `PreparedStatePacket`，为 full snapshot backlog chunk 增加模板与 timestamp patch metadata。

### Wave 4

修改 full snapshot backlog 入队逻辑：首次进入 backlog 时提取静态模板。

### Wave 5

修改 full snapshot backlog drain 逻辑：按当前 dispatch tick patch timing，再生成最终发送 bytes。

### Wave 6

构建、跑单测与聚焦回归、写 cleanup receipt。

## Verification Commands

```bash
env CCACHE_DISABLE=1 script/build_server.sh --debug
ctest --test-dir server/build-debug --output-on-failure -R "state_sync_backlog|dispatch_cache|event_dispatch_backlog|channel_policy|server_smoke|udp_sync_smoke|tcp_event_batch_smoke|state_packet_budget_smoke|delta_packet_budget_smoke|event_backpressure_smoke|room_sync_budget_smoke|full_snapshot_chunking_smoke"
```

## Delivery Acceptance

- 只有在新增测试先失败后转绿，且 full snapshot / budget / chunking 相关回归仍通过时，才允许声称本阶段完成。

## Rollback Rules

- 若实现过程中发现必须改客户端对 tick 的解释、或需要改 protobuf 结构，立即停止并回到设计层重审。
