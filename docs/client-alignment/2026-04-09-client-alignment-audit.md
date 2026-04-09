# 2026-04-09 客户端对齐审计

## 结论

基于当前仓库代码、协议和 smoke 结果，客户端需要对齐的内容可以分成四类：

1. `必须改`：8 项，里面有 5 项是直接影响当前联机闭环的 P0。
2. `协同项`：1 项，单改客户端无法真正闭环。
3. `已基本对齐`：4 项，可以复用现有实现，不建议重复重做。
4. `纯服务端内部优化，无需客户端改`：5 项。

本次动态验证已跑通：

- `ctest --test-dir server/build-debug --output-on-failure -R "server_smoke|udp_sync_smoke|tcp_event_batch_smoke|full_snapshot_chunking_smoke|upgrade_resume_smoke"`：通过

## 审计方法

- 读协议：`proto/message.proto`
- 读服务端发送/分发：`server/src/network/tcp/session_auth.cpp`、`server/src/network/tcp/session_gameplay.cpp`、`server/src/network/tcp/session_room.cpp`、`server/src/game/managers/game_manager_sync.cpp`、`server/src/game/managers/game_manager_sync_dispatch.cpp`、`server/src/game/managers/game_manager_event_dispatch.cpp`
- 读客户端接收/消费：`client/core/src/main/java/com/lawnmower/Main.java`、`client/core/src/main/java/com/lawnmower/screens/GameScreen.java`、`client/core/src/main/java/com/lawnmower/screens/MainMenuScreen.java`、`client/core/src/main/java/com/lawnmower/screens/RoomListScreen.java`、`client/core/src/main/java/com/lawnmower/screens/GameRoomScreen.java`
- 跑服务端 smoke：重连、UDP delta、batch 事件、full snapshot 分片、升级恢复

## 一、必须改

### P0-1. 补齐 TCP 控制消息的顶层分发

#### 现在的问题

- `client/core/src/main/java/com/lawnmower/Main.java:103-161` 的 TCP 网络线程只解析了登录、重连、房间列表、房间广播、开局、状态同步、单条事件、升级三类消息。
- 它没有解析：
  - `MSG_S2C_CREATE_ROOM_RESULT`
  - `MSG_S2C_JOIN_ROOM_RESULT`
  - `MSG_S2C_LEAVE_ROOM_RESULT`
  - `MSG_S2C_SET_READY_RESULT`
  - `MSG_S2C_PLAYER_HURT_BATCH`
  - `MSG_S2C_ENEMY_DIED_BATCH`
  - `MSG_S2C_PLAYER_LEVEL_UP_BATCH`
  - `MSG_S2C_HEARTBEAT`
- `Main.java:777-845` 明明已经有 `MSG_S2C_CREATE_ROOM_RESULT` 和 `MSG_S2C_SET_READY_RESULT` 的 UI 分支，但因为上游 TCP 线程没 parse，这两个分支实际上到不了。
- 客户端全局也没有任何 `JoinRoomResult` / `LeaveRoomResult` / batch 事件处理代码命中。

#### 服务端/协议证据

- `proto/message.proto:53-84` 已定义上述消息类型。
- `server/src/network/tcp/session_room.cpp:61-139` 会返回 `CreateRoomResult / JoinRoomResult / LeaveRoomResult / SetReadyResult`。
- `server/src/game/managers/game_manager_event_dispatch.cpp:709-726` 当前发送的是 `PLAYER_HURT_BATCH / ENEMY_DIED_BATCH / PLAYER_LEVEL_UP_BATCH`。
- 服务端代码中已经找不到 `MSG_S2C_PLAYER_HURT / MSG_S2C_ENEMY_DIED / MSG_S2C_PLAYER_LEVEL_UP` 的实际发送路径。

#### 需要对齐什么

- 在 `Main.java` 顶层 TCP 收包 switch 中补齐这些消息的 parse。
- 在 `handleNetworkMessage()` 中补齐 `JoinRoomResult / LeaveRoomResult / batch 事件 / Heartbeat` 的分发。
- 保持“顶层 parse 能到达 UI 分支”这一闭环，不要只在某个下游类里补 handler。

### P0-2. 开局失败时不能误进 GameScreen

#### 现在的问题

- `server/src/network/tcp/session_gameplay.cpp:69-74` 明确表示：`TryStartGame` 失败时，服务端仍会发 `MSG_S2C_GAME_START`，但 `success=false`，且只回给发起者。
- `client/core/src/main/java/com/lawnmower/Main.java:808-823` 收到 `MSG_S2C_GAME_START` 后直接：
  - `prepareUdpClientForMatch()`
  - 切到 `GameScreen`
  - 期待 full sync
- 它没有先检查 `S2C_GameStart.success`。

#### 需要对齐什么

- 先判断 `success`。
- 若失败：
  - 留在 `GameRoomScreen`
  - 展示 `message_start`
  - 不启动 UDP
  - 不重置 world / 不进入游戏态
- 若成功，再沿用现有进入 `GameScreen` 的逻辑。

### P0-3. 批量事件要真正消费，不能继续只等旧单条事件

#### 现在的问题

- `GameScreen.java:3438-3466` 只处理单条：
  - `MSG_S2C_PLAYER_HURT`
  - `MSG_S2C_ENEMY_DIED`
  - `MSG_S2C_PLAYER_LEVEL_UP`
- 但服务端现在发的是 batch。

#### 服务端/测试证据

- `server/src/game/managers/game_manager_event_dispatch.cpp:709-726` 把三类关键事件包装成 batch。
- `server/tests/integration/tcp_event_batch_smoke_test.cpp:533-557` 动态验证的是 `MSG_S2C_PLAYER_HURT_BATCH`。

#### 需要对齐什么

- 在 `Main.java` 接入三类 batch 消息。
- 在 `GameScreen.java` 新增 batch handler，把 `events` 逐条喂给现有单条处理函数，或者直接重写成批量消费。
- 批量事件到达顺序要保持消息内顺序，不要为了“复用旧函数”打乱顺序。

### P0-4. 状态合并必须真正消费 `authoritative_tick`

#### 现在的问题

- 协议已经给 `PlayerState / EnemyState / ItemState / 对应 delta` 都加了 `authoritative_tick`。
- 服务端文档 `server/docs/AI_GUIDE.md:176-190` 明确写了语义：
  - `sync_time.tick` 是“包发出在哪一帧”
  - `authoritative_tick` 是“对象最后一次真实权威变化在哪一帧”
  - backlog / 分片 / 续发时，包 tick 可以前进，但对象 tick 不该倒退
- 客户端代码里完全没有任何 `authoritative_tick` 消费逻辑：
  - `mergePlayerDelta()`：`client/core/src/main/java/com/lawnmower/screens/GameScreen.java:3017-3052`
  - `mergeEnemyDelta()`：`GameScreen.java:3060-3088`
  - `applyItemState / materializeItemDelta()`：`GameScreen.java:446-616`
  - 全文件搜不到 `authoritative_tick` 相关引用

#### 服务端/测试证据

- `proto/message.proto:263,347,364,409,420,431`
- `server/src/game/managers/game_manager_sync.cpp:31-65,388-411,492-509,578-653`
- `server/tests/integration/udp_sync_smoke_test.cpp:598-678`
- `server/tests/integration/full_snapshot_chunking_smoke_test.cpp:368-399`

#### 需要对齐什么

- 为 player / enemy / item 分别维护“最后已应用的对象级 authoritative tick”。
- 应用 full sync、普通 sync、delta、掉落/拾取收敛时，都按对象级 tick 做去旧：
  - 只接受更高 `authoritative_tick`
  - 同一 `authoritative_tick` 的 replay 要幂等
  - 不能只靠包级 `sync_time.tick` 去判旧
- `sync_time.tick` 继续只用于：
  - 包级顺序判定
  - full snapshot 分片装配顺序
  - 统计/插值时间基准

### P0-5. `DroppedItem` 事件的去重策略与服务端 backlog 语义冲突

#### 现在的问题

- `GameScreen.java:530-549` 的 `shouldAcceptDroppedItems()` 以“包 tick 必须严格递增”为前提：
  - `incomingTick <= lastDroppedItemTick` 就丢弃
- 但服务端对非关键事件 backlog 的语义是：
  - backlog 续发时保留原始 `sync_time.tick`
  - 同一原始 tick 的事件还能被拆成多个 chunk
- 这意味着客户端会把“同一 tick 的后续 dropped item chunk”或“延后续发的 backlog dropped item 包”直接丢掉。

#### 服务端/文档证据

- `server/src/game/managers/game_manager_event_dispatch.cpp:384-395` 构造 dropped item 事件并写入同一 `sync_time`
- `server/src/game/managers/game_manager_event_dispatch.cpp:638-654` dropped item 支持切块/排队/backlog
- `server/docs/AI_GUIDE.md:296-301` 明确说明非关键事件 backlog 保留原始 tick

#### 需要对齐什么

- 不要再用“packet tick 严格递增”做 dropped item 的唯一去重条件。
- 改成按 item 级别幂等：
  - `item_id`
  - `authoritative_tick`（若事件里最终靠 `ItemState` 落地）
  - `is_picked/type/position` 的状态收敛
- 要允许：
  - 同一 tick 的多个 dropped item 包都能被处理
  - 原始 tick 较旧、但实际晚到的 backlog 包仍能把缺失掉落补上

### P0-6. “请求全量重同步”目前是伪实现

#### 现在的问题

- `client/core/src/main/java/com/lawnmower/Main.java:553-563` 的 `requestFullGameStateSync()` 发送的是 `MSG_C2S_HEARTBEAT`。
- `server/src/network/tcp/session_auth.cpp:113-126` 对 heartbeat 的处理只是回 `S2C_Heartbeat`，不会触发 full snapshot。
- 客户端自己还完全没消费 `S2C_Heartbeat`。
- `GameScreen.java:3095-3109` 和 `GameScreen.java:3823-3829` 都把这个伪请求当成“delta 基线丢了之后的 full resync 入口”。

#### 需要对齐什么

- 这项不能只写客户端补丁糊过去。
- 至少要做两件事之一：
  1. 新增一个真正的 `C2S_RequestFullSync` 协议与服务端处理。
  2. 明确约定 heartbeat 具备“请求 targeted full sync”的副作用，并同步修改服务端。
- 在协议闭环前，客户端不能再把 heartbeat 叫做“请求全量同步”。

#### 边界

- 这是 `客户端 + 协议/服务端协同项`
- 如果另一个 AI 只改客户端，这一项做不完整

### P0-7. 房间/多人入口必须先登录

#### 现在的问题

- `client/core/src/main/java/com/lawnmower/screens/MainMenuScreen.java:97-101` 点击“多人游戏”直接进入 `RoomListScreen`。
- `RoomListScreen.java:232-240` 一展示就请求房间列表。
- 但服务端房间相关操作都以“已登录”作为前提：
  - `session_room.cpp:66-74`
  - `session_room.cpp:98-106`
  - `session_room.cpp:115-121`
- `HandleGetRoomList()` 在未登录时只会返回空列表：`server/src/network/tcp/session_room.cpp:78-89`

#### 需要对齐什么

- 进入房间列表前先完成登录并拿到：
  - `player_id`
  - `session_token`
- 不要再让“多人游戏”走未登录路径。
- `MainMenuScreen` 需要统一登录入口，而不是现在“单人游戏才登录，多人游戏直接跳房间列表”的分裂逻辑。

### P0-8. 房间控制 UI 不能继续靠乐观切屏硬撑

#### 现在的问题

- 创建房间：
  - `RoomListScreen.java:251-264` 有 `onCreateRoomResult()`，但 TCP 线程上游没 parse，成功/失败结果当前并不可靠
- 加房：
  - `RoomListScreen.java:373-378` 只发请求，不接 `JoinRoomResult`
- 离房：
  - `GameRoomScreen.java:216-221` 发送完立即本地切回 `RoomListScreen`
  - 完全没看 `LeaveRoomResult`
- 准备：
  - `GameRoomScreen.java:160-176` 有失败提示 UI
  - 但上游 TCP 线程没 parse `MSG_S2C_SET_READY_RESULT`

#### 服务端证据

- `server/src/game/managers/room_manager_lifecycle.cpp:66-79`
- `server/src/game/managers/room_manager_lifecycle.cpp:135-147`
- `server/src/game/managers/room_manager_lifecycle.cpp:163-173`
- `server/src/game/managers/room_manager_gameplay.cpp:47-64`

#### 需要对齐什么

- 创建 / 加入 / 离开 / 准备 都改成“以服务端结果为准”。
- 失败时必须显示服务端 message。
- 成功时再做切屏或本地状态更新。
- 不能继续依赖 `ROOM_UPDATE` 广播去“顺便兜底一切控制流结果”。

## 二、协同项

### C1. 真正的 full resync 请求协议

这是上面 `P0-6` 的单列强调版，因为它会影响另一个 AI 的任务边界。

- 现状：
  - 客户端在“初始状态长期收不到”或“delta 基线缺失”时，会调用 `requestFullGameStateSync()`
  - 这个 API 实际只是发 heartbeat
  - 服务端不会因此回 full snapshot
- 结论：
  - 如果目标是“把客户端完整对齐到现在这套服务端语义”，就必须补一个真正的重同步控制面
- 推荐范围表达：
  - `若只允许改客户端：先把假接口标出来，不要假装已经闭环`
  - `若允许协同改协议/服务端：新增显式 full-sync request`

## 三、已基本对齐，可复用现有实现

### A1. 登录后的 `session_token`、UDP 上行鉴权、重连 token 刷新

- 客户端：
  - `Main.java:679-712` 会提取并附带 `session_token`
  - `Main.java:938-959` 会发 UDP hello 注册 endpoint
  - `Main.java:467-516` 会在 `ReconnectAck` 后刷新 token、重建房间上下文
- 服务端：
  - `session_auth.cpp:97-108` 登录下发 token
  - `udp_server.cpp:356-360` 校验 UDP token
  - `session_auth.cpp:221-238` 重连时回填/刷新 token

### A2. full snapshot 分片缓冲、合并后原子覆盖

- 客户端：
  - `GameScreen.java:2119-2280` 已经支持 `snapshot_id/chunk_index/chunk_count` 的缓冲与合并
- 服务端：
  - `game_manager_sync_dispatch.cpp:226-287` 会对 TCP full snapshot 分片
- 动态验证：
  - `full_snapshot_chunking_smoke` 通过

### A3. 升级请求 / 选项 / 刷新 / 选择确认 UI 主链路

- 客户端：
  - `GameScreen.java:1205-1537`
  - `GameScreen.java:2686-2739`
  - `Main.java:569-628`
- 服务端：
  - `game_manager_upgrade.cpp:183-243`
  - `game_manager_upgrade.cpp:246-384`
- 动态验证：
  - `upgrade_resume_smoke` 通过

### A4. 重连后等待 snapshot 的 hold 逻辑

- 客户端：
  - `Main.java:467-516`
  - `GameScreen.java:2614-2627`
  - `GameScreen.java:2550-2552`
- 服务端：
  - `session_auth.cpp:199-245`
- 动态验证：
  - `server_smoke` 通过

## 四、纯服务端内部优化，无需客户端改

以下内容不要喂给另一个 AI 当成“客户端也要跟着改”的范围：

1. `P2-2 channel routing`
   - 服务器内部频道语义层
   - 文档已明确“不改客户端”：`docs/requirements/2026-04-08-p2-2-server-channel-routing.md`

2. `P2-3 server serialization cache`
   - 服务器构包/序列化缓存
   - 文档已明确“不改客户端”：`docs/requirements/2026-04-08-p2-3-server-serialization-cache.md`

3. `P2-3 cross-tick state cache`
   - backlog prepared bytes 复用
   - 文档已明确“不改客户端”：`docs/requirements/2026-04-09-p2-3-cross-tick-state-cache.md`

4. `P2-3 full snapshot backlog template`
   - full snapshot backlog 模板复用
   - 文档已明确“不改客户端”：`docs/requirements/2026-04-09-p2-3-full-snapshot-backlog-template.md`

5. `P2-3 packet object pool / shared string pool / dispatch cache`
   - 纯发送热路径优化
   - 文档已明确“不改客户端”：`docs/requirements/2026-04-09-p2-3-packet-object-pool.md`

客户端真正要做的，不是复刻这些内部优化，而是正确消费它们带来的**外部可见语义**：

- batch 事件
- full snapshot 分片
- backlog / replay 下的对象级 `authoritative_tick`
- 非关键事件的延后到达

## 五、建议直接交给另一位 AI 的实施范围

如果你要把这份文档直接喂给另一位 AI，建议给它的实现范围写成下面这样：

### 必改文件

- `client/core/src/main/java/com/lawnmower/Main.java`
- `client/core/src/main/java/com/lawnmower/screens/GameScreen.java`
- `client/core/src/main/java/com/lawnmower/screens/MainMenuScreen.java`
- `client/core/src/main/java/com/lawnmower/screens/RoomListScreen.java`
- `client/core/src/main/java/com/lawnmower/screens/GameRoomScreen.java`

### 可能需要协同改动的文件

- `proto/message.proto`
- `server/src/network/tcp/session_auth.cpp`
- 或新增一个显式 full sync request 的服务端处理文件

### 实施优先级

1. 先补 `Main.java` 的消息 parse/dispatch 闭环
2. 再补 `GameStart.success=false`、房间控制结果驱动、多人入口登录
3. 再补 batch 事件消费
4. 再补 `authoritative_tick` 驱动的对象级去旧/幂等
5. 最后处理 full resync 协同项

## 六、给另一位 AI 的一句话摘要

当前客户端不是“完全没接新协议”，而是“已经接了一半”：`重连 / 升级 / full snapshot 分片` 基本有了，但 `房间控制结果、batch 事件、authoritative_tick、真实 full resync` 这四块还没闭环，其中 `full resync` 不是纯客户端问题。
