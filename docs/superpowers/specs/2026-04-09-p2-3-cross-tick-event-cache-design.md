# 2026-04-09 P2-3 Cross-Tick Event Cache Design

## Summary

本设计是服务器侧 `P2-3` 的第二阶段，只覆盖一个更窄但真实可落地的目标：

- 对“延迟非关键事件 backlog”引入跨 tick 的 prepared bytes 复用

本轮不扩展到整条状态同步链，也不做 protobuf 对象池化。

## Current State

`2026-04-08` 的 `P2-3` 第一阶段已经完成：

- 同房间同 tick 的 prepared packet cache
- 状态同步与事件分发接入统一 materialize / reuse 入口
- `dispatch cache` key 已扩充到 `family + slot + message_type + transport`
- 非关键事件 backlog 在 drain 后会自动移除空房间记录

但当前仍有一个明确边界：

- `game_manager_sync_dispatch.cpp` 在发送前会通过
  `FillSyncTiming / FillDeltaTiming` 重写 `sync_time.tick` 与
  `server_time`
- 这意味着状态包当前不适合直接跨 tick 复用旧 bytes

与此相对：

- `game_manager_event_dispatch.cpp` 中的 backlog 事件在入队后会保留原始
  `event_tick` 与原始消息内容
- backlog 跨 tick drain 时，消息内容本身是稳定的
- 但现在每次 drain 仍会重新 materialize TCP framed bytes

## Approach Options

### Option A: 只给延迟事件 backlog 挂持久 prepared bytes

做法：

- backlog 包在首次入队时就保存已序列化 framed bytes
- 后续跨 tick drain 直接复用

优点：

- 改动面最小
- 只覆盖当前真正“跨 tick 内容稳定”的路径
- 不需要修改状态同步 timing 语义

缺点：

- 仍然不是“全链路跨 tick cache”

### Option B: 给 dispatch cache 增加全局跨 tick persistent mode

做法：

- 在房间级 cache 模块里新增跨 tick 持久键空间

优点：

- 结构更统一

缺点：

- 对当前范围来说偏重
- 容易把状态包也一起卷进去，边界变模糊

### Option C: 连状态包也一起做跨 tick 复用

做法：

- 重构状态包 timing 编码或做字段级 patch

优点：

- 理论收益更大

缺点：

- 风险明显更高
- 容易碰到协议编码细节
- 不适合本轮

## Recommendation

采用 `Option A`。

原因：

- 与当前代码事实一致
- 能直接消除“延迟事件 backlog 每个 tick 重新构包”的重复工作
- 不需要扩大到状态同步 timing 语义

## Design

### 1. Scope Boundary

本轮只处理：

- 非关键事件 backlog
  - `ProjectileSpawn`
  - `ProjectileDespawn`
  - `DroppedItem`
  - `EnemyAttackStateSync`

本轮不处理：

- `S2C_GameStateSync`
- `S2C_GameStateDeltaSync`
- 关键 batch 事件
- protobuf object pooling

### 2. Backlog Packet Model

扩展 `game_manager_event_dispatch.cpp` 里的 `PreparedPacket`：

- 保留原始 message variant
- 新增 backlog 级 prepared bytes 缓存槽
  - `std::shared_ptr<const std::string>` framed bytes
  - `payload_len`
  - `body_len`

语义：

- 当 packet 被放入 backlog 时，若它属于跨 tick 稳定消息，则立即 materialize
  一次并保存在 packet 自身
- 后续跨 tick drain 时直接复用该 bytes，不再重新序列化

### 3. Send Rules

发送逻辑分两种：

1. 当前 tick 立即发送的事件
   - 继续使用现有同 tick cache 逻辑

2. backlog 中延后发送的事件
   - 若 packet 已携带 prepared bytes，直接复用
   - 若没有，再补一次 materialize，并写回 packet

### 4. Cleanup Rules

以下场景必须同步释放 backlog packet 持有的 prepared bytes：

- backlog drain 完成并移除 packet
- `game_over`
- 房间删除
- 显式 `ClearRoomDispatchState(room_id)`

### 5. Testing

本轮至少需要：

1. 一个失败测试
   - backlog packet 在跨 tick 保留时，prepared bytes identity 不变

2. 一个失败测试
   - backlog drain 完成后，对应房间记录被移除

3. 聚焦回归
   - `event_dispatch_backlog`
   - `tcp_event_batch_smoke`
   - `event_backpressure_smoke`

## Risks

- 若把当前 tick 临时 packet 与 backlog 持久 packet 混用，容易造成生命周期混乱
- 若清理点漏掉，backlog 持有的大 buffer 可能停留过久

## Mitigations

- 只给 backlog packet 增加持久 prepared bytes
- 当前 tick 立即发送 packet 继续沿用原有逻辑
- 房间删除 / `game_over` / backlog drain 后统一清理
