# LawnMower Server AI Guide

面向需要理解和扩展服务器的 AI/开发者，概述核心流程、模块职责与编码范式（C++20）。

## 架构总览
- **网络层**：`TcpServer` 负责监听/接受连接；`TcpSession` 负责单个客户端的收发、包装/拆包、消息分发。使用 Asio 异步 IO，包格式为 `[uint32_t length][Packet protobuf payload]`。
- **房间层**：`RoomManager`（线程安全单例）管理房间创建/加入/准备/离开，生成房间快照和广播房间变更，开始游戏时校验房主与准备状态。
- **游戏层**：`GameManager`（线程安全单例）按房间创建场景、分配出生点、接收玩家输入队列、按固定 tick 推进（玩家移动、敌人生成/寻路/移动）并定期广播状态同步。
- **协议**：位于仓库根目录 `proto/message.proto`，编译生成的 `message.pb.*` 提供所有消息/状态结构体。消息类型枚举 `MessageType` 驱动服务器分发。

## 主要数据流
- 登录：`MSG_C2S_LOGIN` → 校验重复登录 → 回执 `MSG_S2C_LOGIN_RESULT`，分配自增 `player_id`。
- 心跳：`MSG_C2S_HEARTBEAT` → 回执 `MSG_S2C_HEARTBEAT`（服务器时间、当前活跃会话数）。
- 房间：创建/加入/离开/准备分别回执对应 `S2C_*`，房间内成员变更统一广播 `MSG_S2C_ROOM_UPDATE`。
- 开局：房主发送 `MSG_C2S_START_GAME` → 校验 → 生成房间快照 → `GameManager::CreateScene` → 广播 `MSG_S2C_GAME_START` + 初始 `MSG_S2C_GAME_STATE_SYNC`，随后进入逻辑循环。
- 输入：玩家发送 `MSG_C2S_PLAYER_INPUT`，服务端入队（按照 `input_seq` 去重）并在逻辑帧内推进位置、限制边界，定期增量/全量同步 `MSG_S2C_GAME_STATE_SYNC`。
- 敌人：服务端在 tick 内按波次/速率生成敌人，并基于寻路/追踪逻辑更新敌人坐标；敌人状态同样通过 `MSG_S2C_GAME_STATE_SYNC` 的 `enemies` 字段广播。

## 线程安全与时间
- 房间/游戏状态通过 `std::mutex` 保护；网络 IO 依赖 Asio 的单线程 `io_context` 驱动。
- 会话/玩家计数使用 `std::atomic`；时间点使用 `std::chrono`，tick 间隔通过 `asio::steady_timer` 触发。

## 编码范式（C++20）
- 全局启用 `-std=c++20`，尽量使用现代工具：`std::array`、`std::string_view`、`std::optional`、`[[nodiscard]]` 等。
- 模块分层：网络 (`include/src/network/tcp`)、房间管理 (`game/managers/room_manager.*`)、游戏逻辑 (`game/managers/game_manager.*`)、入口 (`src/main.cpp`)。
- 避免改变协议/逻辑的同时拆分职责；使用 `spdlog` 记录关键路径（错误/警告/调试）。
- 配置：`config/server_config.json` 支持读取地图尺寸、tick 频率与移动速度（`GameManager::LoadConfigFromFile`）。

## 扩展提示
- 新消息类型：在 `TcpSession::handle_packet` 中增加 case，并在相应管理器实现处理逻辑；广播时优先使用弱引用会话列表。
- 新场景/玩法：在 `GameManager` 内新增场景属性或 tick 处理，保持线程安全和输入去重。
- 新敌人类型：在 `server/include/game/entities/enemy_types.hpp` 增加配置，并在 `GameManager` 的生成逻辑中选择对应 `type_id`。
- 调试：`SPDLOG_ACTIVE_LEVEL` 已在 CMake 中设为 `DEBUG`, 无需修改
- 注释问题： 不要轻易删除注释，除非它错误/位置有问题，若需移动代码，请把注释也同步过去
