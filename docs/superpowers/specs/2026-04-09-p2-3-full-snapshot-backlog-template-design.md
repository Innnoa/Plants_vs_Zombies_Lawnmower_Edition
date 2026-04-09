# 2026-04-09 P2-3 Full Snapshot Backlog Template Design

## Summary

本设计只覆盖服务器侧 `P2-3` 的下一阶段：对 `full snapshot backlog` 引入跨 tick 的模板复用，同时保持 `sync_time.tick / server_time` 仍表示“实际发送 tick”。

上一轮已经完成：

- 非关键事件 backlog 的跨 tick prepared bytes 复用
- 非 `full snapshot` 状态 backlog 的跨 tick prepared bytes 复用

当前剩余的重点，是 `full snapshot backlog`：

- 不能冻结旧 tick
- 但又希望避免每次 drain 都重新序列化整份大对象列表

## Current State

当前仓库已有以下事实：

- `full snapshot` chunk 会通过 `SplitFullSnapshotForTcp` 预先切分
- `state_packet_budget_smoke` 明确约束：
  - 同一 tick 的全量分片包数不能超预算
  - 因此 backlog 中的 `full snapshot` 包在未来 tick 发出时，`sync_time.tick`
    仍需表现为“实际发送 tick”
- 上一轮尝试把 backlog 包一律冻结 timing，会直接打破这个约束

所以：

- `full snapshot backlog` 不能直接复用整包 frozen bytes
- 但其静态对象部分在跨 tick 期间其实是稳定的

## Approach Options

### Option A: 模板化静态 payload + 发送时 patch timing

做法：

- backlog 入队时保存 full snapshot chunk 的静态模板
- 真正发送时只重编码 `Timestamp`
- 复用静态 payload，不再完整重序列化 players / enemies / items

优点：

- 保留实际发送 tick 语义
- 避免重复重序列化大对象列表
- 不需要改客户端或协议

缺点：

- 需要维护 timestamp patch metadata

### Option B: backlog 只保存 chunk message，对象不重建，但每次仍完整 SerializeToArray

优点：

- 实现更稳

缺点：

- 主要开销还在
- 不足以支撑当前阶段继续推进 `P2-3`

### Option C: 多 buffer 拼包，发送层直接 scatter/gather

优点：

- 理论收益更高

缺点：

- 会扩散到 `TcpSession / UdpServer`
- 改动面过大，不适合当前阶段

## Recommendation

采用 `Option A`。

原因：

- 同时满足“保留实际发送 tick 语义”和“减少重复序列化”
- 影响面集中在状态同步 dispatch 侧
- 不需要改客户端和协议

## Design

### 1. Scope Boundary

本轮只处理：

- `full snapshot backlog chunk`
- backlog chunk 的模板化复用
- 发送时 timestamp patch

本轮不处理：

- 当前 tick 立即发送的 full snapshot
- 非 `full snapshot` backlog
- 事件 backlog
- protobuf 对象池化

### 2. Packet Model

给 `PreparedStatePacket` 中属于 `full snapshot backlog` 的包增加模板字段，例如：

- `template_transport`
- `template_payload` 或 `template_packet_bytes`
- `timestamp_patch_meta`

其中：

- `template_payload`
  - 表示不包含最终动态 timing 的静态部分
- `timestamp_patch_meta`
  - 记录 `sync_time` 在模板中的 patch 位置信息
  - 以及必要的长度重算信息

### 3. Send Rules

发送分两类：

1. 当前 tick 立即发送的 `full snapshot`
   - 保持现有逻辑

2. 进入 backlog 的 `full snapshot chunk`
   - 入队时构造模板
   - 真正发送时只按当前 `dispatch tick`
     重新编码新的 `Timestamp`
   - 再将动态 timing 和静态模板拼成最终 TCP framed / UDP packet bytes

### 4. Timing Semantics

本轮明确保持：

- `full snapshot backlog` 的 `sync_time.tick / server_time`
  仍表示“实际发出 tick / 实际发送时间”

这和上一轮的非 `full snapshot backlog frozen timing` 是不同语义，必须明确区分。

### 5. Cleanup Rules

以下场景必须同时释放：

- `template_payload`
- `timestamp_patch_meta`
- 相关动态发送缓冲

触发条件：

- backlog chunk drain 完成
- `ClearRoomDispatchState(room_id)`
- 房间删除
- `game_over`

### 6. Testing

先写失败测试，再补实现。

本轮至少需要：

1. `full_snapshot_backlog_tick_updates`
   - backlog chunk 跨 tick 发送时，`sync_time.tick` 会更新为实际发送 tick

2. `full_snapshot_backlog_template_reuse`
   - backlog chunk 跨 tick 保持同一份模板 identity

3. 继续复用：
   - `state_packet_budget_smoke`
   - `full_snapshot_chunking_smoke`
   - `room_sync_budget_smoke`

## Risks

- protobuf 编码长度如果处理不严，patch 后可能形成非法包
- 若模板与动态 timing 的拼接边界不清，可能比当前实现更脆弱

## Mitigations

- 只在 backlog full snapshot chunk 上启用模板化
- 用单元测试明确验证：
  - tick 更新
  - 模板 identity 复用
- 用现有 smoke 验证：
  - budget
  - chunking
  - room packet 节流
