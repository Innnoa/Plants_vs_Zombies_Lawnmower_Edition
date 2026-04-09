# 2026-04-09 P2-3 Full Snapshot Backlog Template Requirement

## Goal

完成服务器侧 `P2-3` 的下一阶段：为 `full snapshot backlog` 引入跨 tick 模板复用，在保持实际发送 tick 语义的前提下，减少 backlog drain 时对完整快照对象列表的重复序列化。

## Deliverable

- 服务器代码改动
- 至少一个新增 failing-first 测试，覆盖 full snapshot backlog 跨 tick 的 tick 更新与模板 identity 复用
- 更新后的构建与测试结果

## Constraints

- 只改服务器
- 不改客户端
- 不改 `proto/message.proto` 外层包结构
- 保持 `full snapshot` 的 `sync_time.tick / server_time` 仍表示实际发送 tick
- 不做 protobuf arena 或 message 池化

## Acceptance Criteria

- backlog 中的 full snapshot chunk 会保存可跨 tick 复用的静态模板
- full snapshot chunk 在未来 tick 发送时，`sync_time.tick` 仍更新为实际发送 tick
- backlog 跨 tick drain 时不会重新完整序列化整份对象列表
- `state_packet_budget_smoke / full_snapshot_chunking_smoke / room_sync_budget_smoke` 仍通过

## Manual Spot Checks

- `server/src/game/managers/game_manager_sync_dispatch.cpp`
- `server/include/game/managers/internal/game_manager_sync_dispatch.hpp`
- `server/tests/unit/state_sync_backlog_test.cpp`
- `server/tests/integration/state_packet_budget_smoke_test.cpp`
- `server/tests/integration/full_snapshot_chunking_smoke_test.cpp`
- `server/tests/integration/room_sync_budget_smoke_test.cpp`

## Non-Goals

- 非 full snapshot backlog
- 事件 backlog
- 客户端改动
- 协议字段变更
- protobuf 对象池化
