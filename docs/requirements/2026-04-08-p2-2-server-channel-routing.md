# 2026-04-08 P2-2 Server Channel Routing Requirement

## Goal

完成服务器侧 `P2-2` 第一阶段：在不修改 protobuf 外层结构、不改客户端的前提下，引入独立的服务器内部频道语义层，把现有 TCP/UDP 传输选择与 fallback/backlog/budget 规则收敛到统一策略表。

## Deliverable

- 服务器内部频道枚举与策略查询入口
- 把输入上行、状态同步分发、关键事件分发改为依赖统一 channel policy
- 至少一个 policy 单元测试
- 至少一个集成 smoke / 回归测试，证明关键事件与高频状态仍走正确链路

## Constraints

- 只改服务器
- 不改 `proto/message.proto` 外层包结构
- 不新增可靠 UDP 或应用层重传
- 不改客户端消费

## Acceptance Criteria

- 存在统一的 `MessageType -> ChannelPolicy` 权威映射
- 服务器网络代码不再在多个位置直接写死“某消息一定 TCP / 一定 UDP”的核心决策
- `C2S_PlayerInput`、`S2C_GameStateDeltaSync`、全量快照、关键 batch 事件都被明确分到预期频道
- 现有 `server_smoke / udp_sync_smoke / tcp_event_batch_smoke` 至少一组回归通过

## Manual Spot Checks

- `server/src/network/tcp/tcp_session.cpp`
- `server/src/network/udp/udp_server.cpp`
- `server/src/game/managers/game_manager_sync_dispatch.cpp`
- `server/src/game/managers/game_manager_event_dispatch.cpp`
- 新增 `server/include/network/channel_policy.hpp`
- 新增 `server/src/network/channel_policy.cpp`

## Non-Goals

- 客户端频道语义
- 协议外壳字段变更
- 可靠 UDP / 应用层重传
