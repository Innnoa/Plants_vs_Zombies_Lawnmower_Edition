## Analysis

- 仓库当前由 `server/`、`client/`、`proto/`、`game_config/`、`script/` 等目录组成，本次“服务器结构”任务的核心范围明确落在 `server/`，并依赖根目录 `proto/message.proto` 与 `game_config/*.json`。
- 服务端实际启动入口在 `server/src/main.cpp`：先加载 `server_config`、`player_roles`、`enemy_types`、`items_config`、`upgrade_config`，再装配 `GameManager`、`RoomManager`、`UdpServer`、`TcpServer`，最后运行单一 `asio::io_context` 主循环。
- 服务端构建体系集中在 `server/CMakeLists.txt`：定义了 `proto_lib`、主程序 `server` 以及三个 smoke test 目标 `server_smoke_test`、`udp_sync_smoke_test`、`config_loader_smoke_test`。常用构建入口是 `script/build_server.sh`。
- 目录结构与职责已经比较清晰：
  - `server/include/config` + `server/src/config`：配置结构体与 `game_config/*.json` 加载逻辑。
  - `server/include/network/tcp` + `server/src/network/tcp`：TCP 监听、会话收发、登录/房间/游戏流程分发。
  - `server/include/network/udp` + `server/src/network/udp`：UDP 输入上行与状态广播。
  - `server/include/game/managers` + `server/src/game/managers`：`RoomManager` 管理房间生命周期，`GameManager` 管理场景、tick、战斗、同步、升级、重连等权威逻辑。
  - `server/tests/integration` 与 `server/tests/unit`：现有 smoke tests，覆盖 TCP 主流程、UDP 同步、配置容错。
- 现有文档 `server/docs/AI_GUIDE.md` 已经描述了一版服务端结构，但仓库根目录缺少统一 `AI_GUIDE.md`，且 `README.md` 的项目结构示例与当前真实目录存在潜在漂移风险，因此后续检查不能只基于文档。

## Plan

1. 建立服务端结构基线。
   - 以 `server/src/main.cpp`、`server/CMakeLists.txt`、`script/build_server.sh` 为主线，确认当前服务端的启动链、构建链和测试链。
   - 输出一份“目录层级 + 运行时装配”的结构图草案，避免只停留在文件树视角。

2. 按职责梳理四个核心层级。
   - 配置层：梳理 `server/include/config/*.hpp` 与 `server/src/config/*.cpp` 如何映射到 `game_config/*.json`。
   - 网络层：梳理 `TcpServer`、`TcpSession`、`session_auth.cpp`、`session_room.cpp`、`session_gameplay.cpp` 与 `UdpServer` 的边界。
   - 房间层：梳理 `RoomManager` 负责的房间、会话、开局状态切换逻辑。
   - 游戏层：梳理 `GameManager` 及其 `game_manager_*.cpp` 子模块如何承接 tick、同步、战斗、升级和重连。

3. 交叉核对文档与真实代码。
   - 对照 `server/docs/AI_GUIDE.md` 与 `README.md`，找出结构描述是否与实际目录、入口文件、测试布局一致。
   - 若发现漂移，优先记录差异点，再决定是否补文档，而不是直接改业务代码。

4. 规划交付物与最小改动面。
   - 预期的主要产物应是文档而非源码修改，优先考虑新建或更新服务端结构说明文档，例如 `server/docs/server_structure_audit.md`。
   - 同步更新 `AI_GUIDE.md` 的摘要与审计记录，必要时再修正 `README.md` 中已经失真的服务端结构描述。
   - 除非结构检查过程中发现明确的代码组织缺陷并得到额外确认，否则不计划修改 `server/src/**` 或 `server/include/**` 业务实现。

5. 验证结构结论可落地。
   - 如进入执行阶段，优先运行项目已有入口验证结构判断：
     - `script/build_server.sh --configure-only`
     - `script/build_server.sh --debug`
     - `ctest --test-dir server/build-debug --output-on-failure`
   - 若环境不允许执行构建或测试，则保留静态检查结论，并明确说明验证缺口。

## Risks

- `GameManager` 的职责拆分较细，部分私有结构通过 `.inc` 与 `internal/*.hpp` 拼装，结构梳理时很容易只看到文件数量而忽略真实调用关系。
- `server/docs/AI_GUIDE.md` 和 `README.md` 可能存在历史信息残留，若直接引用会把旧结构误认为当前结构。
- “检查服务器结构”可能同时指目录结构、模块职责和运行时链路；如果只输出其中一个维度，结果会不完整。
- 现有 smoke tests 主要证明行为可运行，不直接证明模块边界合理，因此结构结论仍需要人工对照入口代码确认。
