## Analysis

- 当前 worktree、`origin/main`、相关 task 分支，以及 git 历史中都没有找到 `server/docs/net_fix` 或同名工作清单。
  - 已检查：`find . -path '*net_fix*'`、`git ls-files | rg 'net_fix'`、`git log --all --name-only | rg 'server/docs/.+net_fix'`。
  - 因此本次“完成度”只能基于当前服务器代码、现有服务器文档和测试结果重建，不能按原始清单逐项打勾。

- 当前服务器主链路已经完整落地，且入口清晰：
  - `server/src/main.cpp`
    - 负责加载 `server_config`、`player_roles`、`enemy_types`、`items_config`、`upgrade_config`。
    - 装配单一 `asio::io_context`、`UdpServer`、`TcpServer`、`RoomManager`、`GameManager`。
  - `server/src/network/tcp/tcp_session.cpp`
    - 已分发登录、心跳、重连、建房、房间列表、加房、离房、准备、退出、开局、输入、升级相关消息。
  - `server/src/network/tcp/session_auth.cpp`
    - 已实现登录、心跳、重连。
  - `server/src/network/tcp/session_room.cpp`
    - 已实现建房、加房、离房、房间流。
  - `server/src/network/tcp/session_gameplay.cpp`
    - 已实现开局、TCP 输入兜底、升级确认/选择/刷新。
  - `server/src/network/udp/udp_server.cpp`
    - 已实现基于 `session_token` 的 UDP 输入校验、endpoint 注册、`GameStateSync` / `GameStateDeltaSync` 广播。
  - `server/src/game/managers/room_manager_*.cpp`
    - 已实现房间生命周期、准备态、开局校验、结束回房间态。
  - `server/src/game/managers/game_manager_*.cpp`
    - 已实现场景创建、tick、敌人、战斗、同步、升级、断线重连恢复、性能指标落盘。

- 当前服务器“已实现且已验证”的部分完成度较高，核心网络链路可视为基本完成：
  - `server/tests/integration/server_smoke_test.cpp`
    - 覆盖未登录保护、登录、建房、房间列表、加房、准备、开局、观察者视角、重连。
  - `server/tests/integration/udp_sync_smoke_test.cpp`
    - 覆盖登录、建房、开局、携带 `session_token` 的 UDP 输入、`GameStateDeltaSync` 接收。
  - `server/tests/unit/config_loader_smoke_test.cpp`
    - 覆盖 `server_config`、`player_roles`、`enemy_types`、`items_config`、`upgrade_config` 的容错与默认值回退。

- 动态核对结果：
  - 构建命令：`CCACHE_DIR=/tmp/ccache CCACHE_TEMPDIR=/tmp/ccache/tmp TMPDIR=/tmp/ccache/tmp script/build_server.sh --debug`
  - 测试命令：`ctest --test-dir build-debug --output-on-failure`
  - 实际结果：`server_smoke`、`udp_sync_smoke`、`config_loader_smoke` 共 3/3 通过。
  - 说明：
    - 服务器当前不是“只有代码骨架”，而是核心 TCP/UDP/房间/开局/同步链路确实可运行。
    - 首次构建失败原因是沙箱下 `~/.cache/ccache/tmp` 只读；切到 `/tmp` 后构建通过。这是环境问题，不是服务器代码缺陷。

- 目前更像“剩余验证缺口”，而不是“主功能未实现”：
  - 现有 integration smoke 没有覆盖 `Heartbeat`。
  - 现有 integration smoke 没有覆盖升级完整链路：`UpgradeRequestAck`、`UpgradeOptionsAck`、`UpgradeSelect`、`UpgradeRefreshRequest`。
  - 现有 integration smoke 没有覆盖 `GameOver -> RoomManager::FinishGame -> 回房间态`。
  - 现有 integration smoke 没有覆盖 `server_metrics/` 落盘结果。
  - 缺少一个权威的 `server/docs/net_fix` 清单来标记“已实现 / 已验证 / 待补验证”。

- 基于当前证据的完成度判断：
  - 如果标准是“服务器基础联机主链路是否已完成并可运行”，答案是：基本完成。
  - 如果标准是“服务器所有对外协议和关键 gameplay 分支都有自动化验证”，答案是：未完成，主要缺测试与清单，不是缺主链路实现。

## Plan

1. 先补服务器侧权威清单
   - 新建或恢复 `server/docs/net_fix` 目录下的检查文档，避免后续继续靠聊天或零散文档判断完成度。
   - 清单建议至少拆成：
     - 启动与配置
     - TCP 登录 / 心跳 / 重连
     - 房间流（建房、房间列表、加房、离房、准备、开局）
     - UDP 输入注册与状态同步
     - 场景 / tick / 敌人 / 战斗 / GameOver
     - 升级链路
     - 指标落盘与文档一致性
   - 每项标记成：
     - `已实现且已验证`
     - `已实现但未验证`
     - `未实现`

2. 再补服务器 integration 验证缺口
   - 优先新增或扩展服务器 smoke test，覆盖：
     - 心跳
     - 升级完整流程
     - GameOver 后房间状态恢复
     - `server_metrics/` 文件落盘
   - 优先复用现有测试模式：
     - `server/tests/integration/server_smoke_test.cpp`
     - `server/tests/integration/udp_sync_smoke_test.cpp`

3. 若测试补齐后发现真实缺口，再回到服务端实现修补
   - 可能涉及的文件：
     - `server/src/network/tcp/session_auth.cpp`
     - `server/src/network/tcp/session_gameplay.cpp`
     - `server/src/game/managers/game_manager_upgrade.cpp`
     - `server/src/game/managers/game_manager_tick_flow.cpp`
     - `server/src/game/managers/room_manager_gameplay.cpp`
     - `server/src/game/managers/game_manager_metrics.cpp`

4. 等服务器侧清单和验证基线稳定后，再做客户端对齐
   - 客户端后续应对齐“服务器已验证行为”，而不是反过来猜服务端协议。
   - 这样可以避免客户端适配一个尚未被服务器测试锁定的行为集。

## Risks

- `server/docs/net_fix` 原始清单缺失，因此当前“完成度”属于基于代码与测试的重建结论，不是对原清单的逐项验收。
- 现有 smoke test 证明了服务器主链路可运行，但不能替代升级、结算、指标落盘这些分支的专项验证。
- 如果后续补测时发现协议边界条件问题，可能会暴露出“实现已存在但状态机细节不稳”的问题，尤其是升级暂停态和 GameOver 收尾阶段。
- 当前构建依赖可写缓存目录；在受限环境中需要显式把 `CCACHE_DIR` / `TMPDIR` 指向可写路径，否则会误判成构建失败。
