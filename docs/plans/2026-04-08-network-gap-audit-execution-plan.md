# 2026-04-08 Network Gap Audit Execution Plan

## Internal Grade

`L`

理由：这是一次基于现有仓库的串行审计任务，不需要多代理，也不需要并行写入。

## Waves

### Wave 1

读取仓库结构、现有审计文档、协议定义与服务端关键发送路径，建立 P0/P1/P2 与代码入口的映射。

### Wave 2

核对客户端接收与消费规则，重点确认：

- 是否使用 `authoritative_tick`
- 是否支持批量关键事件
- 是否严格按 `is_full_snapshot` 处理全量
- 是否已经做全量分片的缓冲与原子覆盖

### Wave 3

若本地可构建，则执行最相关的服务端 build / smoke tests，补充动态验证。

### Wave 4

整理最终矩阵，输出未做项、部分完成项、以及服务端/客户端边界。

## Ownership Boundaries

- 主代理负责全部审计、验证与最终结论。
- 本次不启用子代理。

## Verification Commands

```bash
rg -n "S2C_GameStateDeltaSync|S2C_GameStateSync|authoritative_tick|is_full_snapshot|batch|room_packet_budget_per_tick" server client proto
script/build_server.sh --debug
ctest --test-dir server/build-debug --output-on-failure -R "udp_chunking_smoke|full_snapshot_chunking_smoke|delta_packet_budget_smoke|state_packet_budget_smoke|event_backpressure_smoke|enemy_sync_throttle_smoke|room_sync_budget_smoke|tick_dispatch_gate"
```

## Delivery Acceptance Plan

- 只有在代码或测试能支撑时，才允许使用“已做”表述。
- 服务器已做但客户端未消费的项，统一归类为“部分完成”。
- 仅存在字段或缓存、未形成完整闭环的项，统一归类为“未做”或“基础已在，闭环未完成”。

## Completion Language Rules

- 不说“全部完成”，除非 P0/P1/P2 逐项都有端到端证据。
- 优先回答“没做的还有哪些”，再补“已经做了哪些，做到什么程度”。

## Rollback Rules

- 本次只新增治理与审计产物，不改业务代码。
- 若用户不需要这些产物，可单独删除本次新增文档与 receipts。

## Phase Cleanup Expectations

- 生成 skeleton / intent / lineage / cleanup receipts。
- 记录实际执行的验证命令与是否成功。
