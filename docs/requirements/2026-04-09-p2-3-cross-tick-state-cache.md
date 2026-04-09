# 2026-04-09 P2-3 Cross-Tick State Cache Requirement

## Goal

完成服务器侧 `P2-3` 的下一阶段：对状态同步链路里的 backlog 包引入跨 tick 的 prepared bytes 复用，减少延迟状态包在未来 tick drain 时的重复构包与拷贝。

## Deliverable

- 服务器代码改动
- 至少一个新增 failing-first 测试，覆盖状态 backlog 跨 tick 的 prepared bytes identity
- 更新后的构建与测试结果

## Constraints

- 只改服务器
- 不改客户端
- 不改 `proto/message.proto` 外层包结构
- 同时覆盖 TCP / UDP backlog 包
- 只对进入 backlog 的状态包冻结 timing
- 不做 protobuf arena 或 message 池化

## Acceptance Criteria

- 进入 backlog 的状态包会保存可跨 tick 复用的 TCP/UDP prepared bytes
- backlog 跨 tick drain 时不会重新 materialize 相同状态包的 bytes
- backlog 状态包的 `sync_time.tick / server_time` 固定为首次入 backlog 时的语义
- 房间清理、`game_over` 或 backlog drain 后，对应持久 bytes 会一并释放

## Manual Spot Checks

- `server/src/game/managers/game_manager_sync_dispatch.cpp`
- `server/include/game/managers/internal/game_manager_sync_dispatch.hpp`
- `server/tests/unit/state_sync_backlog_test.cpp`
- `server/tests/integration/state_packet_budget_smoke_test.cpp`
- `server/tests/integration/delta_packet_budget_smoke_test.cpp`
- `server/tests/integration/room_sync_budget_smoke_test.cpp`

## Non-Goals

- 事件 backlog
- 客户端改动
- 协议字段变更
- protobuf 对象池化
