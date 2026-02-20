# LawnMower Server AI Guide（当前版本）

更新日期：2026-02-20  
执行者：Codex

本指南面向需要理解、排障与扩展当前服务器的开发者/AI。  
内容以当前仓库实现为准，重点覆盖目录结构、行为模式、文件职责、构建体系和依赖。

## 1. 架构摘要

服务器采用权威服模式：

1. TCP 负责可靠控制链路与关键事件（登录、房间、开局、升级流程、GameOver 等）。
2. UDP 负责高频状态同步与输入上行（玩家输入、状态广播）。
3. `RoomManager` 负责房间生命周期，`GameManager` 负责场景与战斗权威逻辑。
4. 配置驱动运行参数，配置来源固定为根目录 `game_config/*.json`。

协议定义位于根目录 `proto/message.proto`，服务器构建时生成 `message.pb.*` 并统一使用。

## 2. 目录结构与职责

### 2.1 头文件层（对外接口）

`server/include/`

1. `config/*.hpp`：配置结构体定义与加载函数声明。
2. `network/tcp/*.hpp`：TCP 服务器/会话接口。
3. `network/udp/*.hpp`：UDP 服务器接口。
4. `game/managers/*.hpp`：`RoomManager`、`GameManager` 的核心接口与运行时结构。

说明：`include` 目录是稳定接口层，供主程序与多翻译单元共享。

### 2.2 实现层

`server/src/`

1. `main.cpp`
   - 启动入口。
   - 加载全部配置、初始化日志、创建 TCP/UDP 服务。
   - 绑定 `GameManager` 与 `RoomManager` 所需上下文。

2. `config/*.cpp`
   - 解析 `game_config/*.json`。
   - 提供字段类型校验、范围 clamp、默认值回退。
   - 解析失败会输出警告并退回默认配置。

3. `network/tcp/`
   - `tcp_server.cpp`：监听与接受连接。
   - `tcp_session.cpp`：收包/拆包/分发主干、写队列与断连处理。
   - `session_auth.cpp`：登录、心跳、重连、token 管理。
   - `session_room.cpp`：建房/加房/离房/准备/房间列表。
   - `session_gameplay.cpp`：开局、输入、升级相关协议处理。

4. `network/udp/udp_server.cpp`
   - 接收 UDP 输入并做 token 校验。
   - 维护 `player_id -> endpoint` 映射（带 TTL）。
   - 广播 `S2C_GameStateSync` / `S2C_GameStateDeltaSync`。

5. `game/managers/`
   - `room_manager.cpp`：房间管理、玩家映射、开局校验、结束回房间态。
   - `game_manager_*.cpp`：场景、tick、战斗、同步、升级、重连、性能统计等分模块实现。

6. `game/managers/internal/*.hpp`
   - manager 子模块私有头（仅 `src/game/managers` 内部引用）。
   - 当前包含：
     - `game_manager_event_dispatch.hpp`
     - `game_manager_internal_utils.hpp`
     - `game_manager_misc_utils.hpp`
     - `game_manager_sync_dispatch.hpp`

### 2.3 测试与文档

1. `server/tests/server_smoke_test.cpp`：TCP 主流程 smoke（登录、房间、重连等）。
2. `server/tests/udp_sync_smoke_test.cpp`：UDP 同步与收敛相关 smoke。
3. `server/tests/config_loader_smoke_test.cpp`：配置加载容错与边界测试。
4. `server/docs/`：服务器侧文档。

说明：`server/tests/integration` 与 `server/tests/unit` 目录目前预留，尚未放置用例。

## 3. 运行时行为模式（端到端）

### 3.1 启动

1. 读取 `server_config`、`player_roles`、`enemy_types`、`items_config`、`upgrade_config`。
2. 解析失败不会中断进程，保留默认值并记录 `warn`。
3. 启动 `UdpServer` 与 `TcpServer`，进入 `io_context.run()` 主循环。

### 3.2 登录与会话

1. `MSG_C2S_LOGIN` 分配 `player_id` 与 `session_token`。
2. `session_token` 用于 TCP/UDP 请求校验。
3. TCP 异常断线默认保留 token（用于重连宽限期）；主动退出会撤销 token。

### 3.3 房间流

1. 建房/加房/离房/准备均走 TCP。
2. 房间成员变更统一通过 `MSG_S2C_ROOM_UPDATE` 广播。
3. `Room` 内部有 `player_index_by_id` 索引，玩家查找不是线性扫描。

### 3.4 开局与场景创建

1. 房主发起 `MSG_C2S_START_GAME`。
2. 服务端校验“房主身份 + 全员 ready + 房间状态”。
3. 创建场景后广播 `MSG_S2C_GAME_START`，并发送首帧全量状态。
4. 随后启动房间级固定 tick 循环（`asio::steady_timer`）。

### 3.5 Tick 主循环

每帧大致顺序：

1. 消费玩家输入队列（含输入时间预算与防堆积策略）。
2. 敌人更新（刷怪、寻路、移动、死亡清理）。
3. 道具更新（拾取判定、效果结算）。
4. 战斗推进（开火、射弹推进/命中、近战伤害、掉落、GameOver 判定）。
5. 升级流程触发与暂停态处理。
6. 同步包构建（全量/增量）与事件分发。
7. 性能采样与可选落盘。

### 3.6 升级流程（暂停态）

当前实现是“请求 -> 选项 -> 选择/刷新 -> 恢复”的链路：

1. 服务端触发 `S2C_UpgradeRequest` 后，场景置 `is_paused=true`。
2. 客户端回 `C2S_UpgradeRequestAck` 后，服务端发 `S2C_UpgradeOptions`（固定 3 选项）。
3. 客户端回 `C2S_UpgradeOptionsAck`，进入等待选择阶段。
4. 客户端回 `C2S_UpgradeSelect` 后，服务端应用强化，回 `S2C_UpgradeSelectAck`，并在流程结束时恢复游戏。
5. 若还有待处理升级会继续下一个请求；否则恢复并推送一次全量状态。
6. 刷新请求 `C2S_UpgradeRefreshRequest` 受 `refresh_limit` 控制。

### 3.7 断线重连

1. 玩家断线后进入宽限期（`reconnect_grace_seconds`）。
2. 宽限期内使用 `C2S_ReconnectRequest` + token 可恢复会话。
3. 重连成功后返回 `S2C_ReconnectAck`，游戏中会补发当前全量状态。
4. 超过宽限期会清理玩家状态与 token。

### 3.8 游戏结束

1. 先广播 `S2C_GameOver`，再调用 `RoomManager::FinishGame` 将房间 `is_playing=false`。
2. 结束时会保存本局性能统计到 `server_metrics/<日期>/...json`。

## 4. 同步模型（当前关键约定）

### 4.1 两类同步消息

1. `S2C_GameStateSync`
   - 用于低频变化同步与必要快照。
   - 在 `is_full_snapshot=true` 时才代表全量快照。
2. `S2C_GameStateDeltaSync`
   - 高频增量同步，玩家/敌人/道具按 delta 字段传输。

### 4.2 脏数据追踪

当前已采用向量化脏队列：

1. `Scene` 内 `dirty_player_ids/dirty_enemy_ids/dirty_item_ids` 为 `vector`。
2. 各 runtime 带 `dirty_queued` 去重标记，避免重复入队。
3. 这一模式替代了旧的 `unordered_set` 脏集合，降低同步热路径哈希开销。

### 4.3 发送策略

1. 优先 UDP 广播。
2. UDP 不可用或目标缺失时走 TCP 兜底。
3. 为降低丢包影响，关键场景会使用强制同步窗口（如新生成对象的多次续发）。

## 5. 构建体系

### 5.1 CMake 目标

`server/CMakeLists.txt` 主要定义：

1. `proto_lib`（由 `proto/*.proto` 自动生成并编译）。
2. `server`（主可执行）。
3. `server_smoke_test`、`udp_sync_smoke_test`、`config_loader_smoke_test`。

### 5.2 构建目录约定（固定四目录）

当前统一使用以下四个目录：

1. `server/build-debug`
   - 默认 Debug 构建目录（`script/build_server.sh --debug`）。
2. `server/build-gcc`
   - GCC 工具链目录（`script/build_server.sh --gcc`）。
3. `server/build-clang`
   - Clang 工具链目录（`script/build_server.sh --clang`）。
4. `server/build-asan`
   - ASan/UBSan 验证目录（由重构验证脚本使用）。

### 5.3 常用构建与测试命令

在仓库根目录：

```bash
script/build_server.sh --debug
script/build_server.sh --gcc
script/build_server.sh --clang
```

在 `server/` 目录：

```bash
ctest --test-dir build-debug --output-on-failure
```

重构验证（format/tidy/build/test/asan）：

```bash
~/.codex/skills/run-refactor-cycle/scripts/run_refactor_cycle.sh
```

## 6. 依赖清单

1. Protobuf
   - 协议代码生成与运行时序列化。
   - 配置 JSON 解析（`google::protobuf::util::JsonStringToMessage`）。
2. Asio
   - TCP/UDP 异步网络与定时器调度。
3. spdlog
   - 统一日志系统（支持异步 logger）。
4. Abseil（可选）
   - CMake 中按可用性探测并链接，缺失时不阻塞构建。

## 7. 配置体系（权威路径）

服务端读取顺序支持相对路径回退，但权威配置目录是根目录 `game_config/`：

1. `game_config/server_config.json`
2. `game_config/player_roles.json`
3. `game_config/enemy_types.json`
4. `game_config/items_config.json`
5. `game_config/upgrade_config.json`

注意：

1. `server/config` 已不作为运行时配置来源。
2. 配置解析失败时会有 `warn`，并自动回退默认值。

## 8. 扩展入口建议

按需求类型选择改动入口：

1. 网络协议分发扩展：`server/src/network/tcp/tcp_session.cpp`
2. 登录/重连：`server/src/network/tcp/session_auth.cpp`
3. 房间行为：`server/src/network/tcp/session_room.cpp` + `server/src/game/managers/room_manager.cpp`
4. 战斗/射弹/掉落：`server/src/game/managers/game_manager_combat.cpp`
5. 同步策略：`server/src/game/managers/game_manager_sync.cpp` + `server/src/game/managers/game_manager_sync_dispatch.cpp`
6. Tick 编排：`server/src/game/managers/game_manager_tick_flow.cpp`
7. 升级流程：`server/src/game/managers/game_manager_upgrade.cpp`
8. 配置项新增：`server/include/config/*.hpp` + `server/src/config/*.cpp` + `game_config/*.json`

## 9. 当前版本注意事项

1. `S2C_GameStateSync` 不是“每帧全量”，全量语义依赖 `is_full_snapshot` 标记。
2. 道具、敌人、玩家的同步一致性依赖 delta 与低频同步共同收敛，客户端需按“状态合并”处理。
3. `server/include/game/entities/enemy_types.hpp` 当前为历史静态定义，运行时配置以 `game_config/enemy_types.json` 为准。
4. manager 私有头已规范到 `server/src/game/managers/internal/`，不建议暴露到 `include/`。
