# 2026-04-09 P2-3 Packet Object Pool Design

## Summary

本设计只覆盖服务器侧 `P2-3` 的最后一块轻量对象池化范围：给发送热路径里的 `lawnmower::Packet` 引入对象池，减少高频构包时反复创建 / 销毁临时 `Packet` 实例。

上一轮已经完成：

- 同房间同 tick 的 prepared packet cache
- 事件 backlog 与状态 backlog 的跨 tick prepared bytes 复用
- full snapshot backlog 的模板复用

当前剩余的对象池化目标，是最小且风险最低的一层：

- 只池化 `lawnmower::Packet`
- 只覆盖发送热路径

## Current State

当前仓库已有以下事实：

- `tcp_session.cpp` 发送路径会频繁创建 `lawnmower::Packet`
- `udp_server.cpp` 发送路径也会频繁创建 `lawnmower::Packet`
- `game_manager_dispatch_cache.cpp` 和
  `game_manager_sync_dispatch.cpp` 仍有构包热点

这些对象生命周期都很短：

- 设置 `msg_type`
- 写 `payload`
- `SerializeToArray`
- 结束后即销毁

这类模式适合做轻量对象池。

## Approach Options

### Option A: 只池化 `lawnmower::Packet`

做法：

- 新增一个简单对象池
- 热路径改为从池里获取 `Packet`
- 使用后归还

优点：

- 改动最小
- 风险最低
- 收益直接

缺点：

- 不覆盖更大的业务消息对象

### Option B: 连业务 message 一起池化

做法：

- `Packet` 之外，再池化 `S2C_GameStateSync / DeltaSync / 事件 batch`

优点：

- 理论收益更大

缺点：

- 生命周期更复杂
- 容易把当前稳定路径重新打散

### Option C: 直接使用 protobuf arena

做法：

- 更全面地迁移到 arena / 自定义 allocator

优点：

- 理论最彻底

缺点：

- 改动太大
- 与当前阶段不匹配

## Recommendation

采用 `Option A`。

原因：

- 这是当前风险最低的一步
- 能继续推进 `P2-3`，但不把范围扩成更大的 protobuf 生命周期改造

## Design

### 1. Scope Boundary

本轮只处理：

- `lawnmower::Packet`
- 发送热路径中的临时构包对象

本轮不处理：

- 业务 message 对象池化
- 接收链路 `Packet` 解析
- protobuf arena

### 2. Pool Model

新增一个轻量内部工具，例如：

- `server/include/network/shared/packet_object_pool.hpp`

提供：

- `AcquireReusablePacket()`
- RAII 归还句柄

行为：

- 取出时先 `Clear()`
- 用完自动归还
- free list 有保守上限
- 超出上限则直接销毁

### 3. Integration Points

接入点只改以下发送热路径：

- `server/src/network/tcp/tcp_session.cpp`
- `server/src/network/udp/udp_server.cpp`
- `server/src/game/managers/game_manager_dispatch_cache.cpp`
- `server/src/game/managers/game_manager_sync_dispatch.cpp`

改法：

- 把 `lawnmower::Packet packet;` 改为从池里 acquire
- 其余构包逻辑保持不变

### 4. Cleanup Rules

对象池只负责：

- 复用 `Packet` 实例
- 清空状态

不持有：

- 序列化后的 bytes
- 业务 message
- backlog 生命周期状态

### 5. Testing

本轮至少需要：

1. 一个单元测试
   - 归还后可复用同一地址

2. 一个单元测试
   - `Clear()` 后旧字段不残留

3. 聚焦回归
   - `packet_object_pool`
   - `dispatch_cache`
   - `state_sync_backlog`
   - `event_dispatch_backlog`
   - `server_smoke`
   - `udp_sync_smoke`
   - `tcp_event_batch_smoke`
   - 各类 budget / chunking smoke

## Risks

- 若 `Clear()` 不彻底，可能残留旧 payload / msg_type
- 若池对象跨线程归还不安全，可能引入竞态

## Mitigations

- 单元测试明确验证清理语义
- 初版只用简单 mutex + free list
- 不做 lock-free 或更激进优化
