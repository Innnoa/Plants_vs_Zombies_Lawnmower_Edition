# 服务端结构审计

更新日期：2026-03-30  
审计方式：静态代码扫描 + 构建入口核对 + 现有测试入口核对

## 1. 结论摘要

当前服务端结构可以概括为四层：

1. 配置层：`server/include/config/*.hpp` 与 `server/src/config/*.cpp` 负责把根目录 `game_config/*.json` 解析为运行时配置。
2. 网络层：`server/src/network/tcp/*` 负责可靠控制链路，`server/src/network/udp/*` 负责高频输入与状态广播。
3. 房间层：`RoomManager` 负责房间生命周期、成员关系、准备态和开局校验。
4. 游戏层：`GameManager` 负责场景创建、固定 tick、敌人/战斗/升级/同步/性能统计等权威逻辑。

服务端真实启动链路在 `server/src/main.cpp`，构建链路在 `server/CMakeLists.txt` 与 `script/build_server.sh`，现有验证入口是三个 smoke test：

- `server/tests/integration/server_smoke_test.cpp`
- `server/tests/integration/udp_sync_smoke_test.cpp`
- `server/tests/unit/config_loader_smoke_test.cpp`

## 2. 目录结构

```text
server/
├── CMakeLists.txt
├── docs/
│   ├── AI_GUIDE.md
│   └── server_structure_audit.md
├── include/
│   ├── config/
│   ├── game/managers/
│   │   └── internal/
│   └── network/
│       ├── tcp/
│       └── udp/
├── src/
│   ├── config/
│   ├── game/managers/
│   ├── network/
│   │   ├── tcp/
│   │   └── udp/
│   └── main.cpp
└── tests/
    ├── integration/
    └── unit/
```

说明：

- `server/include/` 是跨翻译单元共享的接口层，不只是“声明”，也包含 `GameManager` 的私有拆分入口 `include/game/managers/internal/*.inc`。
- `server/src/` 是实现层，职责已经按配置、网络、房间、游戏逻辑拆分。
- `server/tests/` 目录并非空壳，当前 smoke test 已经分别落在 `integration/` 与 `unit/` 子目录。

## 3. 启动与装配链路

`server/src/main.cpp` 的当前装配顺序如下：

1. 加载 `server_config`、`player_roles`、`enemy_types`、`items_config`、`upgrade_config`。
2. 创建单一 `asio::io_context`。
3. 将配置注入 `GameManager` 与 `RoomManager`。
4. 创建 `UdpServer` 并注入给 `GameManager`。
5. 创建 `TcpServer`。
6. 初始化 `spdlog` 异步 logger。
7. 调用 `udp_server.Start()`、`tcp_server.start()`，最后进入 `io.run()`。

这意味着当前服务端不是“多入口”结构，而是单进程、单 `io_context` 驱动的权威服。

## 4. 模块职责

### 4.1 配置层

关键文件：

- `server/include/config/server_config.hpp`
- `server/include/config/player_roles_config.hpp`
- `server/include/config/enemy_types_config.hpp`
- `server/include/config/item_types_config.hpp`
- `server/include/config/upgrade_config.hpp`
- `server/src/config/*.cpp`

职责：

- 从根目录 `game_config/*.json` 读取配置。
- 处理类型校验、范围 clamp、默认值回退。
- 配置加载失败时返回默认值并记录 `warn`，不阻塞服务端进程启动。

### 4.2 TCP 网络层

关键文件：

- `server/include/network/tcp/tcp_server.hpp`
- `server/include/network/tcp/tcp_session.hpp`
- `server/src/network/tcp/tcp_server.cpp`
- `server/src/network/tcp/tcp_session.cpp`
- `server/src/network/tcp/session_auth.cpp`
- `server/src/network/tcp/session_room.cpp`
- `server/src/network/tcp/session_gameplay.cpp`

职责拆分：

- `tcp_server.cpp`：监听端口并创建 `TcpSession`。
- `tcp_session.cpp`：包头/包体收发、写队列、连接关闭、消息分发主干。
- `session_auth.cpp`：登录、心跳、重连、token 管理。
- `session_room.cpp`：创建房间、获取房间列表、加入/离开房间、准备状态。
- `session_gameplay.cpp`：开始游戏、输入上报、升级链路、首帧全量同步兜底。

### 4.3 UDP 网络层

关键文件：

- `server/include/network/udp/udp_server.hpp`
- `server/src/network/udp/udp_server.cpp`

职责：

- 接收 `MSG_C2S_PLAYER_INPUT`。
- 校验 `player_id` 与 `session_token`。
- 维护 `player_id -> endpoint` 的短期映射。
- 广播 `MSG_S2C_GAME_STATE_SYNC` 与 `MSG_S2C_GAME_STATE_DELTA_SYNC`。

### 4.4 房间层

关键文件：

- `server/include/game/managers/room_manager.hpp`
- `server/src/game/managers/room_manager.cpp`
- `server/src/game/managers/room_manager_lifecycle.cpp`
- `server/src/game/managers/room_manager_session.cpp`
- `server/src/game/managers/room_manager_gameplay.cpp`

职责拆分：

- `room_manager.cpp`：单例入口、房间会话枚举、玩家房间查询。
- `room_manager_lifecycle.cpp`：创建房间、加入房间、离开房间、房间列表。
- `room_manager_session.cpp`：房间内玩家索引、断线标记、重连绑定、房间广播构建。
- `room_manager_gameplay.cpp`：准备态、开局校验、结束后回房间态。

### 4.5 游戏层

关键文件：

- `server/include/game/managers/game_manager.hpp`
- `server/include/game/managers/internal/*.inc`
- `server/src/game/managers/game_manager*.cpp`

职责拆分：

- `game_manager.cpp`：单例入口、默认场景配置、基础依赖注入。
- `game_manager_scene.cpp`：创建场景、初始化玩家、初始敌人/对象池/导航网格。
- `game_manager_loop.cpp`：逻辑帧调度、timer 管理、启动/停止循环。
- `game_manager_tick_flow.cpp` 与 `game_manager_tick_dispatch.cpp`：每 tick 的执行编排。
- `game_manager_enemy.cpp`：刷怪、寻路、敌人状态推进。
- `game_manager_combat*.cpp`：投射物、近战、掉落、GameOver。
- `game_manager_sync.cpp` 与 `game_manager_sync_dispatch.cpp`：全量/增量同步构建、脏数据队列与广播。
- `game_manager_upgrade.cpp`：升级请求、选项构建、应用强化、暂停与恢复。
- `game_manager_session.cpp`：玩家输入、断线、重连恢复。
- `game_manager_metrics.cpp`：逻辑帧采样与 `server_metrics/` 落盘。
- `game_manager_event_dispatch.cpp` / `game_manager_misc_utils.cpp` / `game_manager_internal_utils.cpp`：事件广播与内部辅助逻辑。

## 5. 运行时主链路

当前服务端的典型运行链路如下：

1. TCP 登录后分配 `player_id` 和 `session_token`。
2. TCP 处理建房、入房、准备和开局请求。
3. `RoomManager::TryStartGame` 校验房主和全员准备状态。
4. `GameManager::CreateScene` 构建房间场景。
5. `TcpSession::SendFullSyncToRoom` 首次发全量状态，并启动 `GameManager::StartGameLoop`。
6. 玩家输入优先经 UDP 上行，TCP 可做可靠兜底。
7. 游戏循环在 `GameManager` 内推进战斗、同步、升级与结束逻辑。

## 6. 本轮发现的文档漂移

已确认的偏差如下：

1. `server/docs/AI_GUIDE.md` 把 smoke test 路径写成了 `server/tests/server_smoke_test.cpp` 等根目录文件，真实路径在 `server/tests/integration/` 与 `server/tests/unit/`。
2. `server/docs/AI_GUIDE.md` 把 manager 私有头描述为位于 `server/src/game/managers/internal/`，真实路径是 `server/include/game/managers/internal/`。
3. `README.md` 的项目结构示例仍引用 `proto/messages.proto`、`server/generated/`、`server/include/utils/` 等旧结构，和当前仓库不一致。
4. `README.md` 的服务端测试命令仍使用 `cd server/build`，与当前 `script/build_server.sh --debug` / `server/build-debug` 约定不一致。

## 7. 维护建议

1. 以后新增或迁移服务端目录时，优先同时更新 `server/docs/AI_GUIDE.md`、`README.md` 与本文件。
2. 若后续继续拆分 `GameManager`，建议在 `server/docs/` 下补一份专门的“tick 与同步链路图”，避免只靠文件名理解职责。
3. 若 smoke test 数量继续增加，建议在 `server/tests/README.md` 明确区分 `unit` 与 `integration` 的入口约定。
