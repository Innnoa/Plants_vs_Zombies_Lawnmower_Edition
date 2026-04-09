# 2026-04-08 P2-3 Server Serialization Cache Requirement

## Goal

完成服务器侧 `P2-3` 的本轮落地版本：引入显式的房间级 prepared packet cache，把状态同步与事件分发的序列化结果收敛到统一缓存层，减少同房间同 tick 内的重复构包与拷贝。

## Deliverable

- 服务器代码改动
- 至少一个新增 failing-first 测试，覆盖房间级 dispatch cache 的命中 / 覆盖 / 清理语义
- 更新后的构建与测试结果

## Constraints

- 只改服务器
- 不改客户端
- 不改 `proto/message.proto` 外层包结构
- 不做跨 tick bytes 复用
- 不引入 protobuf arena 或 message 池化

## Acceptance Criteria

- 存在统一的房间级 prepared packet cache 入口
- 事件分发与状态同步分发都通过该 cache materialize / reuse 可发送 bytes
- 同一房间、同一 dispatch tick 内重复发送相同逻辑 packet 时，能够复用已准备好的 bytes
- 新 tick 不会继续复用旧 tick bytes
- 房间删除或 `game_over` 后会清理对应 cache

## Manual Spot Checks

- `server/include/game/managers/internal/game_manager_dispatch_cache.hpp`
- `server/src/game/managers/game_manager_dispatch_cache.cpp`
- `server/src/game/managers/game_manager_sync_dispatch.cpp`
- `server/src/game/managers/game_manager_event_dispatch.cpp`
- `server/src/game/managers/room_manager_session.cpp`
- `server/tests/unit/*`
- `server/tests/integration/*`

## Non-Goals

- 客户端改动
- 协议字段变更
- 跨 tick 长期缓存
- protobuf arena / message 池化
