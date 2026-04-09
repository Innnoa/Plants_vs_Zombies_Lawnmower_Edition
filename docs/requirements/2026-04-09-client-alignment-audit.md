# 2026-04-09 Client Alignment Audit

## Goal

基于当前仓库里的客户端、服务端、协议、测试与已有审计文档，系统梳理“客户端还需要对齐什么”，并明确哪些是客户端必改、哪些需要客户端与协议/服务端协同、哪些其实是纯服务端内部优化而不需要客户端跟进。

## Deliverable

- 一份可直接交给另一位 AI 的 Markdown 审计文档
- 文档必须按功能列出客户端待对齐项，并附最小代码/协议/测试证据
- 文档必须明确边界：`客户端必改` / `客户端+服务端协同` / `已对齐` / `纯服务端无需改`

## Constraints

- 只基于仓库内代码、协议、测试与文档下结论
- 本轮不修改业务实现，只产出治理文档、审计文档与 runtime receipts
- 不把“服务器已发”误判为“客户端已闭环”
- 不把“服务端内部性能改造”误判为“客户端也要改”

## Acceptance Criteria

- 至少覆盖以下功能面：
  - 登录与房间控制流
  - 开局与游戏开始失败路径
  - UDP/TCP 状态同步
  - `authoritative_tick` 语义
  - full snapshot 分片与原子覆盖
  - 批量事件
  - 升级流程
  - 重连流程
  - 纯服务端内部优化边界
- 每个结论都带最小证据，至少引用协议、服务端发送路径、客户端消费路径三类中的两类
- 若某项单改客户端无法闭环，必须明确标成“协同项”

## Product Acceptance

- 用户能把最终文档直接喂给另一位 AI，且对方无需再重新读完整仓库就能开始实现
- 文档中的范围不会误导另一位 AI 去改本轮其实不需要动的纯服务端部分

## Manual Spot Checks

- `proto/message.proto`
- `client/core/src/main/java/com/lawnmower/Main.java`
- `client/core/src/main/java/com/lawnmower/screens/GameScreen.java`
- `client/core/src/main/java/com/lawnmower/screens/MainMenuScreen.java`
- `client/core/src/main/java/com/lawnmower/screens/RoomListScreen.java`
- `client/core/src/main/java/com/lawnmower/screens/GameRoomScreen.java`
- `server/src/network/tcp/session_auth.cpp`
- `server/src/network/tcp/session_gameplay.cpp`
- `server/src/network/tcp/session_room.cpp`
- `server/src/game/managers/game_manager_sync.cpp`
- `server/src/game/managers/game_manager_sync_dispatch.cpp`
- `server/src/game/managers/game_manager_event_dispatch.cpp`
- `server/tests/integration/server_smoke_test.cpp`
- `server/tests/integration/udp_sync_smoke_test.cpp`
- `server/tests/integration/tcp_event_batch_smoke_test.cpp`
- `server/tests/integration/full_snapshot_chunking_smoke_test.cpp`
- `server/tests/integration/upgrade_resume_smoke_test.cpp`

## Non-Goals

- 本轮不直接修客户端
- 本轮不改协议
- 本轮不改服务端业务逻辑
- 本轮不重写既有服务端设计文档
