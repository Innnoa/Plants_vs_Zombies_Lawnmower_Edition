# 2026-04-09 P2-3 Cross-Tick State Cache Design

## Summary

本设计只覆盖服务器侧 `P2-3` 的下一阶段：对状态同步链路里的 backlog 包引入跨 tick 的 prepared bytes 复用。

上一轮已经完成：

- 同房间同 tick 的 prepared packet cache
- 延迟非关键事件 backlog 的跨 tick prepared bytes 复用

当前剩余目标是把同样的思路补到状态同步 backlog，但边界明确为：

- 只处理进入 backlog 的状态包
- 同时覆盖 TCP framed bytes 与 UDP packet bytes
- 延迟发送时允许冻结为“首次入 backlog 时的 tick / server_time”

## Current State

当前仓库已有以下事实：

- `game_manager_sync_dispatch.cpp` 里存在 `DeferredStateBacklogForRoom`
- 状态包发送前会通过 `RefreshPreparedStatePacketTiming`
  调用 `FillSyncTiming / FillDeltaTiming`
- 这会在每次发送前重写：
  - `sync_time.tick`
  - `sync_time.server_time`
- 因此当前状态 backlog 即使内容没变，跨 tick drain 时仍会重复 materialize TCP/UDP bytes

当前缺失点：

- backlog 状态包没有自带持久 prepared bytes
- backlog 跨 tick drain 时没有可直接复用的 TCP/UDP bytes
- 没有明确的“冻结 timing 后跨 tick 继续发送”的语义

## Approach Options

### Option A: PreparedStatePacket 自带持久 bytes

做法：

- 扩展 `PreparedStatePacket`
- 包进入 backlog 时就冻结 timing
- 同时构造：
  - `prepared_tcp`
  - `prepared_udp`
- 后续跨 tick drain 直接复用

优点：

- 边界最清楚
- 改动集中在 `game_manager_sync_dispatch.cpp`
- 不需要把同 tick cache 和跨 tick backlog cache 混成一层

缺点：

- backlog packet 结构会更重一些

### Option B: 扩展通用 dispatch cache，支持 persistent backlog key

做法：

- 让 `game_manager_dispatch_cache` 增加跨 tick 生命周期

优点：

- 结构表面上更统一

缺点：

- 复杂度更高
- 容易把 backlog 持久 cache 和同 tick 临时 cache 混在一起
- 当前阶段收益不明显

### Option C: 只缓存 payload，再 patch timing 字段

做法：

- 只缓存未带 timing 的序列化结果
- 发送前做字节级 patch

优点：

- 理论上内存占用更省

缺点：

- 依赖 protobuf 编码细节
- 实现脆弱，不适合当前代码库

## Recommendation

采用 `Option A`。

原因：

- 范围最可控
- 与前一轮事件 backlog 的持久 bytes 方案风格一致
- 不会把当前同 tick cache 逻辑搞复杂

## Design

### 1. Scope Boundary

本轮只处理：

- `DeferredStateBacklogForRoom`
- backlog 状态包的 TCP framed bytes
- backlog 状态包的 UDP packet bytes

本轮不处理：

- 当前 tick 立即发送的状态包
- 事件 backlog
- protobuf 对象池化

### 2. Packet Model

给 `PreparedStatePacket` 增加：

- `prepared_tcp`
- `prepared_udp`
- `timing_frozen`
- `frozen_dispatch_tick`

语义：

- 包第一次进入 backlog 时，先写入当时的 timing
- 之后把可用路径的 prepared bytes 预构好并挂在 packet 上
- 若 `timing_frozen=true`，后续 tick drain 不再刷新 timing

### 3. Timing Semantics

本轮明确采用：

- 延迟发送的状态 backlog 包，允许冻结为“首次入 backlog 时”的
  `tick / server_time`

这意味着：

- backlog 包的时间语义不再表示“实际发出 tick”
- 而表示“最初决定发送该状态包时的服务端时间”

这是本轮为了跨 tick prepared bytes 复用作出的有意取舍。

### 4. Send Rules

发送分两类：

1. 当前 tick 立即发送
   - 继续走现有逻辑
   - 发送前刷新 timing
   - 继续使用同 tick dispatch cache

2. backlog 跨 tick drain
   - 如果 `timing_frozen=true` 且存在 `prepared_tcp / prepared_udp`
   - 则直接复用对应 bytes
   - 不再调用 `RefreshPreparedStatePacketTiming`

### 5. Transport Coverage

本轮同时覆盖两条路径：

- TCP
  - `SendFramedPacket`
- UDP
  - `BroadcastPreparedPacket`

若某 backlog packet 当前只会走其中一条路径，则只构造该路径所需 bytes。

### 6. Cleanup Rules

以下场景必须连同 backlog packet 的持久 bytes 一起释放：

- packet drain 完成并出队
- `ClearRoomDispatchState(room_id)`
- 房间删除
- `game_over`

本轮不新增新的全局生命周期容器，继续沿用现有房间级 backlog 清理入口。

### 7. Testing

先写失败测试，再补实现。

本轮至少需要：

1. `state_sync_backlog_tcp_identity`
   - TCP backlog 跨 tick 仍复用同一份 bytes

2. `state_sync_backlog_udp_identity`
   - UDP backlog 跨 tick 仍复用同一份 bytes

3. `state_sync_backlog_frozen_timing`
   - 包延迟到后续 tick 发出时，仍保留首次入 backlog 的 timing 语义

4. 聚焦回归
   - `state_packet_budget_smoke`
   - `delta_packet_budget_smoke`
   - `room_sync_budget_smoke`
   - `udp_sync_smoke`
   - `server_smoke`

## Risks

- 冻结 timing 可能改变“客户端观察到的状态包时间语义”
- 若 backlog 包同时保留 TCP/UDP 两份 bytes，单包内存占用会上升
- 若冻结 timing 的边界没守住，可能误伤当前 tick 立即发送路径

## Mitigations

- 只对 backlog 包启用冻结 timing
- 当前 tick 立即发送路径继续维持现有语义
- 用单元测试明确区分：
  - backlog 跨 tick identity
  - 冻结 timing
  - drain 后释放
