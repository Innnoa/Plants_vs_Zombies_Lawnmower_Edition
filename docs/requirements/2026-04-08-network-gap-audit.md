# 2026-04-08 Network Gap Audit

## Goal

基于仓库当前代码、协议、测试与已有审计文档，核对 `server/docs/net_fix` 中 P0/P1/P2 网络稳定性清单的实现状态，回答“还有哪些没做”。

## Deliverable

输出一份按条目组织的状态结论，至少区分：

- `已做`
- `部分完成 / 仅服务器完成`
- `未做`

每个结论都要附最小代码或测试依据。

## Constraints

- 只能基于仓库内可读取证据下结论，不允许凭印象补全。
- 先看服务端，再核对客户端消费路径，避免把“服务器已发”误判成“端到端已完成”。
- 本次任务默认不实现缺口，只做审计与验证。

## Acceptance Criteria

- P0/P1/P2 每个条目都有状态结论。
- 至少覆盖服务端同步、事件分发、客户端消费、协议定义四类证据。
- 若某项只能证明“服务器已做、客户端未跟上”，必须明确写出边界。
- 若能运行验证命令，则记录实际运行结果；若不能运行，也要说明原因。

## Product Acceptance

- 用户能直接看到还没做的项，以及“差在服务器、客户端还是协议层”。

## Manual Spot Checks

- `server/src/network/udp/udp_server.cpp`
- `server/src/game/managers/game_manager_sync.cpp`
- `server/src/game/managers/game_manager_sync_dispatch.cpp`
- `server/src/game/managers/game_manager_event_dispatch.cpp`
- `server/src/game/managers/game_manager_tick_flow.cpp`
- `client/core/src/main/java/com/lawnmower/Main.java`
- `client/core/src/main/java/com/lawnmower/screens/GameScreen.java`
- `proto/message.proto`
- `server/tests/integration/*`

## Non-Goals

- 本轮不补实现。
- 本轮不重写现有审计文档。
