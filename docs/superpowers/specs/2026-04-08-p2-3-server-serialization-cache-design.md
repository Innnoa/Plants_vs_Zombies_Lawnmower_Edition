# 2026-04-08 P2-3 Server Serialization Cache Design

## Summary

本设计只覆盖服务器侧 `P2-3` 的本轮落地范围：在现有 `shared_string_pool` 和预构包路径基础上，引入显式的“房间级 prepared packet cache”。

目标不是重写协议，也不是做跨 tick 的长期缓存，而是把当前分散在事件分发、状态同步分发、TCP framed 构包、UDP packet 构包中的临时序列化结果，收敛为可复用、可清理、按房间管理的统一缓存层。

本轮重点是：

- 同一房间、同一 dispatch tick 内的 prepared bytes 复用
- full snapshot / delta / 关键事件的显式缓存入口
- 明确缓存失效与房间生命周期清理规则

## Current State

当前仓库已有以下事实：

- 已有 `network/shared/shared_string_pool.hpp`
  - TCP framed buffer
  - UDP packet buffer
  - 事件分发临时 framed bytes
  都已经部分复用 `std::string` 缓冲
- `game_manager_event_dispatch.cpp`
  - 已能先构造 `PreparedPacket`
  - 同一批 prepared packet 会在多 session 之间复用
- `game_manager_sync_dispatch.cpp`
  - 已能先拆 chunk，再按路由发送
  - backlog 延迟发送也已经有房间级队列
- `tcp_session.cpp` 与 `udp_server.cpp`
  - 仍各自保留一套 `ByteSizeLong + SerializeToArray` 构包逻辑

当前缺失点：

- 没有显式的房间级 prepared packet cache
- 没有统一的“逻辑 packet -> 可发送 bytes”的服务器内部缓存入口
- 同步分发与事件分发的序列化复用仍然是局部的、分散的
- backlog 跨 tick 续发时，仍会在不同位置重新 materialize bytes
- 房间结束或房间删除时，没有统一的 cache 清理点

## Approach Options

### Option A: 只抽公共序列化 Helper

做法：

- 统一 TCP framed / UDP packet 的构包函数
- 减少重复代码

优点：

- 风险最小
- 改动面可控

缺点：

- 仍是“现做现发”
- 不能形成显式房间级缓存
- 达不到本轮 `B` 档目标

### Option B: 房间级 Prepared Packet Cache

做法：

- 新增房间级 dispatch cache
- 缓存同一房间、同一 dispatch tick 的 prepared bytes
- 事件分发和状态同步分发都先走 cache，再走实际发送

优点：

- 与当前 `P2-3` 目标最贴合
- 能把已有局部复用收敛成统一结构
- 后续继续做更细的 budget / backlog / snapshot 优化时，有稳定落点

缺点：

- 需要非常明确的 tick 边界与失效规则
- 若边界处理不清，容易复用到旧包

### Option C: 更激进的 Protobuf Message / Arena 池化

做法：

- 连 chunk message 本身也做池化
- 尽量少建临时 protobuf 对象

优点：

- 理论上分配更少

缺点：

- 复杂度明显更高
- 容易引入对象生命周期与状态污染问题
- 不适合作为本轮首选

## Recommendation

采用 `Option B`。

原因：

- 满足“只改服务器”
- 能在现有实现上继续收敛，而不是推倒重来
- 对现有发送语义侵入较小
- 能直接覆盖“继续减少临时对象、批量复用缓冲、按房间缓存序列化结果”的目标

## Design

### 1. Cache Scope And Ownership

新增一个内部模块，例如：

- `server/include/game/managers/internal/game_manager_dispatch_cache.hpp`
- `server/src/game/managers/game_manager_dispatch_cache.cpp`

职责只做一件事：

- 管理“房间级 prepared bytes”

它不负责：

- chunk 拆分策略
- state / event route 决策
- session / UDP endpoint 管理

这些逻辑仍留在现有 dispatch 文件中。

### 2. Cache Model

按 `room_id` 保存 `RoomDispatchCache`。

每个房间缓存只保留最近一个 tick 的两个槽位：

- `event_cache`
- `state_cache`

每个槽位至少包含：

- `dispatch_tick`
- `kind`
- `prepared packets`

每个 prepared packet 至少包含：

- `msg_type`
- `transport`
- `payload_len`
- `body_len`
- `shared_ptr<const std::string>` bytes

其中：

- TCP 路径保存 framed bytes
- UDP 路径保存 `Packet` 序列化后的 bytes

缓存的是“可直接发送的结果”，不是再缓存一份 protobuf message。

### 3. Cache Hit Rules

命中条件只认：

- 同一 `room_id`
- 同一 `dispatch_tick`
- 同一类 payload family

本轮不做：

- 跨 tick bytes 复用
- payload hash 复用
- 长期多版本缓存

原因：

- 当前 `sync_time.tick` 是主排序语义
- 若把旧 tick bytes 继续发到新 tick，风险高于收益

因此：

- 同 tick 内重复发送，直接命中 cache
- backlog 跨 tick 续发时，只允许复用逻辑 chunk，不复用旧 bytes
- backlog 在新 tick 发送前，必须先刷新 timing，再生成该 tick 的 prepared bytes

### 4. State Path Integration

状态链路继续保留当前边界：

- `PrepareDeltaPackets`
- `PrepareSyncPackets`
- `PrepareTargetedSyncPackets`

它们仍然负责：

- chunk 拆分
- 路由选择
- full snapshot / delta 语义判断

变化点在发送阶段：

- `SendPreparedStatePacket` 不再各自临时 materialize bytes
- 而是把“已刷新 timing 的逻辑 packet”交给 dispatch cache
- 由 cache 返回：
  - TCP framed bytes
  - 或 UDP packet bytes

这样：

- 同一 tick 内多次发送同类状态 payload 可复用 prepared bytes
- backlog 跨 tick 续发仍能保持 timing 正确

### 5. Event Path Integration

事件链路继续保留当前逻辑：

- 组装 `TickEventMessages`
- 按 backlog / budget 生成逻辑 packet 列表

变化点在最终发送前：

- 不在事件分发内部重复做分散式 framed 序列化
- 改为把本 tick 的逻辑 packet 列表统一交给 dispatch cache
- 由 cache 返回可直接发送的 TCP framed bytes

同一 tick 内：

- 多 session 广播直接复用同一批 prepared bytes
- GameOver 结束帧也能走统一清理路径

### 6. Invalidation And Cleanup

失效规则采用保守策略：

1. 同房间进入新 tick
   - 直接覆盖旧 `event_cache / state_cache`

2. 房间被删除
   - 显式 `ClearRoomDispatchCache(room_id)`

3. `game_over`
   - 清事件 backlog
   - 同时清该房间 dispatch cache

4. 单房间缓存上界
   - 只保留最近一个 tick 的 cache，避免无限增长

这样内存上界与房间数线性相关，不引入长期累积缓存。

### 7. File Touch Points

本轮主要改动文件建议收敛为：

- 新增 `server/include/game/managers/internal/game_manager_dispatch_cache.hpp`
- 新增 `server/src/game/managers/game_manager_dispatch_cache.cpp`
- 修改 `server/src/game/managers/game_manager_sync_dispatch.cpp`
- 修改 `server/src/game/managers/game_manager_event_dispatch.cpp`

必要时增加一处清理调用：

- `server/src/game/managers/room_manager_session.cpp`

本轮不要求修改：

- `server/src/network/tcp/tcp_session.cpp`
- `server/src/network/udp/udp_server.cpp`

除非实现过程中证明必须抽公共 helper 才能保证边界清晰。

### 8. What Changes Now

这轮将完成：

- 显式房间级 dispatch cache
- 统一的逻辑 packet -> prepared bytes materialize / reuse 入口
- 状态同步与事件分发接入 cache
- 同房间同 tick 内的 prepared bytes 复用
- 房间删除 / GameOver / 新 tick 覆盖时的 cache 清理

### 9. What Does Not Change Now

这轮不会做：

- 客户端改动
- protobuf 协议改动
- 跨 tick bytes 复用
- protobuf arena / message 池化
- 更激进的全局内存池体系

### 10. Error Handling

若 cache materialize 失败：

- 不应静默吞掉
- 输出 warning / debug 日志
- 当前 packet 不发送
- 不允许写入损坏的 cache

若房间 cache 清理点缺失：

- 应优先补显式清理
- 不以“依赖进程结束释放”作为默认策略

### 11. Testing

先写失败测试，再补实现。

本轮至少需要：

1. 一个 dispatch cache 单元测试
   - 同房间同 tick 请求相同 payload 时命中复用

2. 一个 dispatch cache 单元测试
   - 新 tick 覆盖旧 cache，不复用旧 bytes

3. 一个 dispatch cache 单元测试
   - 房间清理后 cache 失效

4. 一个聚焦集成回归
   - 验证同 tick 重复 full sync 或关键事件发送时，行为不变

5. 聚焦现有 smoke 回归
   - `server_smoke`
   - `udp_sync_smoke`
   - `tcp_event_batch_smoke`
   - 以及新增 `dispatch_cache` 相关测试

## Risks

- 如果 cache 键设计不严，可能把旧 tick bytes 错发到新 tick
- 如果清理点漏掉，房间结束后会残留大 buffer
- 如果为了缓存强行重写现有 dispatch 边界，容易引入行为回归

## Mitigations

- cache 键必须带 `dispatch_tick`
- 跨 tick 只复用逻辑 chunk，不复用旧 bytes
- 房间删除 / GameOver / 新 tick 覆盖时都显式清 cache
- chunk 拆分与 route 决策继续留在原 dispatch 文件，只把 bytes materialize / reuse 收敛到新模块
