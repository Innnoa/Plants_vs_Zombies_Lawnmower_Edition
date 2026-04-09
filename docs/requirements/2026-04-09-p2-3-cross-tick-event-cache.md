# 2026-04-09 P2-3 Cross-Tick Event Cache Requirement

## Goal

完成服务器侧 `P2-3` 的第二阶段：对延迟非关键事件 backlog 引入跨 tick 的 prepared bytes 复用，减少 backlog drain 过程中重复 TCP framed 构包。

## Deliverable

- 服务器代码改动
- 至少一个新增 failing-first 测试，覆盖 backlog packet 跨 tick 仍复用同一份 prepared bytes
- 更新后的构建与测试结果

## Constraints

- 只改服务器
- 不改客户端
- 不改 protobuf 外层包结构
- 不扩展到状态同步链路
- 不做 protobuf arena 或 message 池化

## Acceptance Criteria

- 延迟非关键事件 backlog packet 在跨 tick 保留时可复用已准备好的 framed bytes
- backlog drain 时不会重新为同一 packet 重复序列化
- backlog drain 完成后仍会清理空房间记录
- 房间删除或 `game_over` 后，对应 backlog prepared bytes 会被释放

## Manual Spot Checks

- `server/src/game/managers/game_manager_event_dispatch.cpp`
- `server/src/game/managers/internal/game_manager_event_dispatch.hpp`
- `server/tests/unit/event_dispatch_backlog_test.cpp`
- `server/tests/integration/tcp_event_batch_smoke_test.cpp`
- `server/tests/integration/event_backpressure_smoke_test.cpp`

## Non-Goals

- 状态同步跨 tick 复用
- 客户端改动
- 协议字段变更
- protobuf 对象池化
