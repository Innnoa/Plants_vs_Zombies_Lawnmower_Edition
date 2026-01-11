# 核心游戏逻辑需求：场景基础与玩家移动（服务端）

## 目标

- 在现有「登录/房间」框架基础上，补齐「开始游戏 → 初始化场景 → 接收输入 → 广播状态」的最小闭环。
- 服务端作为权威（Authoritative Server）维护玩家坐标，并对移动做边界限制。

## 协议（proto）调整

文件：仓库根目录 `proto/message.proto`

- 新增 `SceneInfo`：描述场景基础信息（尺寸、tick_rate、state_sync_rate）。
- 扩展 `S2C_GameStart`：新增 `success/message_start/scene` 字段，用于开始游戏成功/失败回执以及下发场景信息。
- 扩展 `C2S_PlayerInput`：新增 `input_seq/delta_ms`，用于输入去重与移动步长估计（服务端可回退到固定 dt）。
- 扩展 `S2C_GameStateSync`：新增 `room_id`，便于客户端做状态路由/校验。
- 扩展 `PlayerState`：新增 `move_speed`，便于后续做角色差异化与服务端可配置化。

## 服务端职责与数据流

### 开始游戏

1. 房主发送 `MSG_C2S_START_GAME`
2. 服务端校验（房主身份/房间未开局/成员准备状态）
3. 创建场景（地图尺寸、出生点、初始属性）
4. 广播：
   - `MSG_S2C_GAME_START`（带 `SceneInfo`）
   - `MSG_S2C_GAME_STATE_SYNC`（全量玩家初始状态）

### 玩家移动

1. 客户端发送 `MSG_C2S_PLAYER_INPUT`（包含移动方向 `move_direction`）
2. 服务端根据 `delta_ms`（或固定 dt）计算位移：`pos += dir * move_speed * dt`
3. 对坐标做场景边界 clamp（`[0,width]`、`[0,height]`）
4. 广播 `MSG_S2C_GAME_STATE_SYNC`（增量：仅包含发生移动的玩家 `PlayerState`）

## 服务器代码结构落点（与现有结构对齐）

- `include/game/managers/game_manager.hpp` / `src/game/managers/game_manager.cpp`
  - 场景创建、玩家初始出生、处理输入与坐标更新、构造状态同步包
- `include/game/managers/room_manager.hpp` / `src/game/managers/room_manager.cpp`
  - 新增「开始游戏」校验与房间快照输出（包含会话 weak_ptr，供广播使用）
- `src/network/tcp/tcp_server.cpp`
  - 新增 `MSG_C2S_START_GAME`、`MSG_C2S_PLAYER_INPUT` 分发逻辑与广播

## 后续迭代建议（不在本阶段实现）

- 增加独立的逻辑 Tick 循环（按 `tick_rate` 固定步长推进），并按 `state_sync_rate` 定时广播。
- 引入碰撞/障碍物、同步插值/预测、输入延迟补偿与反作弊策略。
- 场景参数从 `config/server_config.json` 加载并支持热更新。
