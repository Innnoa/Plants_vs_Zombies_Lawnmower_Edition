# 2026-04-08 P2-2 Server Channel Routing Design

## Summary

本设计只覆盖服务器侧 `P2-2` 的第一阶段：在不修改 protobuf 外层结构、不改客户端的前提下，为服务器建立独立的“频道语义层”。

目标是把现在分散在各处的“某类消息走 TCP / 某类消息走 UDP / 是否允许 fallback / 是否受 backlog 与 budget 约束”的规则，从消息实现代码里抽离出来，统一收敛为一套可查询、可复用、可审计的策略表。

## Current State

当前仓库已有事实：

- `README.md` 与 `server/docs/AI_GUIDE.md` 已经形成了一套约定：
  - TCP 用于可靠控制和关键事件
  - UDP 用于高频输入和状态同步
- 但这套约定主要存在于文档和具体代码路径里，不存在独立的“频道层”。
- 现有路由方式是“消息类型 + 调用位置”共同决定传输行为，例如：
  - `TcpSession` 直接处理登录、房间、升级、重连等 TCP 路径
  - `UdpServer` 直接处理玩家输入和状态广播
  - `game_manager_sync_dispatch` / `game_manager_event_dispatch` 内部也写死了一部分 fallback 和 budget 逻辑

当前缺失点：

- 没有统一的频道枚举或策略对象
- 没有“消息类型 -> 频道 -> 传输/降级规则”的单一权威映射
- 传输选择、fallback、backlog、budget 逻辑仍然分散
- 后续如果要扩展为更明确的可靠 UDP 或应用层重传，没有稳定的中间层可承接

## Approach Options

### Option A: Internal Policy Layer Only

做法：

- 新增服务器内部 `message channel policy`
- 把消息先映射到频道，再由频道决定：
  - 默认传输层
  - 是否允许 fallback
  - 是否允许 backlog / budget 降级
  - 是否按房间广播或单会话投递

优点：

- 不改协议
- 不动客户端
- 可以直接改善服务器内部语义一致性
- 为未来第二阶段协议升级预留稳定落点

缺点：

- 外层包结构仍看不出频道语义
- 还不等于“真正的频道化协议”

### Option B: Protocol-Level Channelization Now

做法：

- 直接修改 protobuf 外层或包封装，显式带上频道语义

优点：

- 体系更完整

缺点：

- 需要客户端同步
- 改动面大
- 不适合当前“先只改服务器”的目标

### Option C: Documentation Only

做法：

- 只补文档，不改服务器内部结构

优点：

- 最轻

缺点：

- 不能解决规则散落问题
- 后续继续演化时仍然容易失控

## Recommendation

采用 `Option A`。

原因：

- 满足“只改服务器”
- 能把 P2-2 先做成一个真实可落地的结构改进
- 为将来的协议级频道化保留演进空间

## Design

### 1. Channel Model

新增服务器内部频道枚举：

- `ReliableControl`
- `ReliableCriticalEvent`
- `UnreliableRealtimeState`

含义：

- `ReliableControl`
  - 不能丢
  - 语义必须有序
  - 默认 TCP
- `ReliableCriticalEvent`
  - 不能丢
  - 可按 tick 批量
  - 默认 TCP
- `UnreliableRealtimeState`
  - 允许丢包
  - 允许降级或延后
  - 默认 UDP，必要时可 TCP fallback

### 2. Policy Object

新增服务器内部策略结构，例如：

- `channel`
- `default_transport`
- `allow_fallback`
- `allow_backlog`
- `subject_to_packet_budget`
- `subject_to_event_entry_budget`
- `delivery_scope`

其中：

- `default_transport`
  - `TcpOnly`
  - `UdpOnly`
  - `UdpPreferredTcpFallback`
- `delivery_scope`
  - `SingleSession`
  - `RoomBroadcast`
  - `RoomBroadcastWithFallback`

### 3. Message Classification

第一阶段建议按当前仓库语义做以下分类：

#### ReliableControl

- 登录 / 心跳 / 房间 / 准备 / 开局
- 升级请求与确认
- 重连请求与确认
- `S2C_GameStateSync` 的全量快照

#### ReliableCriticalEvent

- `S2C_GameOver`
- `S2C_PlayerHurtBatch`
- `S2C_EnemyDiedBatch`
- `S2C_PlayerLevelUpBatch`
- 必要的关键纠偏状态补发

#### UnreliableRealtimeState

- `C2S_PlayerInput`
- `S2C_GameStateDeltaSync`
- 非关键的高频状态广播

### 4. Integration Points

第一阶段只改服务器内部以下入口：

- `server/src/network/tcp/tcp_session.cpp`
- `server/src/network/udp/udp_server.cpp`
- `server/src/game/managers/game_manager_sync_dispatch.cpp`
- `server/src/game/managers/game_manager_event_dispatch.cpp`
- 必要时增加一个新的 `network/channel_policy.*`

目标：

- 让调用方先取 policy，再决定走哪条传输路径
- 减少“直接按消息类型写死行为”的分支

### 5. What Changes Now

这轮实现将完成：

- 统一的频道枚举与策略查询入口
- 状态同步分发改为通过 policy 决定路由
- 事件分发改为通过 policy 标明是否关键/是否可 backlog
- 输入上行路径也纳入频道分类

### 6. What Does Not Change Now

这轮不会做：

- 改 `proto/message.proto` 外层包结构
- 新增频道字段到 protobuf
- 新增可靠 UDP
- 应用层重传
- 客户端消费改动

### 7. Error Handling

若某消息类型未配置 policy：

- 服务器应使用显式保守默认值
- 输出 warning 或 debug 日志
- 不允许静默落到不确定行为

### 8. Testing

第一阶段至少需要：

1. 一个 policy 单元测试
   - 验证关键消息被分类到正确频道

2. 一个状态同步集成 smoke
   - 验证 `GameStateDeltaSync` 仍走不可靠高频状态路径

3. 一个关键事件集成 smoke
   - 验证关键事件仍走可靠链路，不被错误纳入 UDP 逻辑

4. 一个回归测试
   - 复用现有 `server_smoke / udp_sync_smoke / tcp_event_batch_smoke`

## Risks

- 如果 policy 设计过细，第一阶段会过度抽象
- 如果 policy 与现有 dispatch 代码边界划分不清，可能造成重复判断
- 如果默认值不保守，可能把关键消息错误地降到不可靠通道

## Mitigations

- 第一阶段只定义当前仓库真实需要的三个频道
- 默认值宁可保守走 TCP，也不默认放宽
- 优先改“决策入口”，不在第一轮重写所有分发代码
