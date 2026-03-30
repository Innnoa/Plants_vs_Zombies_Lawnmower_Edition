## Analysis

- 仓库是客户端/服务端同仓结构：
  - 服务端位于 `server/`，C++20 + Asio + Protobuf，`server/src/main.cpp` 会同时启动 TCP 与 UDP 服务，端口来自 `game_config/server_config.json`。
  - 客户端位于 `client/`，Java 17 + LibGDX + Protobuf，网络实现实际使用 `java.net.Socket` 与 `DatagramSocket`。
- 当前真实联通链路已经存在，但分散在多处：
  - `client/core/src/main/java/com/lawnmower/Main.java`
    - 启动时立即通过 `TcpClient.connect()` 建立 TCP 连接。
    - 收到 `MSG_S2C_GAME_START` 后调用 `prepareUdpClientForMatch()` 启动 UDP。
    - 首次 UDP 注册依赖 `sendInitialUdpHello()`，本质是发送一个携带 `session_token` 的 `C2S_PlayerInput`。
  - `client/core/src/main/java/com/lawnmower/network/TcpClient.java`
    - 封装 TCP connect、protobuf 包发送与阻塞接收。
  - `client/core/src/main/java/com/lawnmower/network/UdpClient.java`
    - 封装 UDP socket、接收线程、protobuf packet 发送与回调。
- 当前客户端配置存在明显缺口：
  - `client/core/src/main/java/com/lawnmower/Config.java` 将 `SERVER_HOST` 硬编码为 `192.168.1.13`，TCP/UDP 端口也写死。
  - `README.md` 宣称存在 `client_config.json`，但仓库根并不存在该文件，文档与实现不一致。
  - 这意味着“检查当前服务器与客户端联通程度”无法稳定切换目标环境，也不适合自动化执行。
- 服务端已经有可复用的协议级烟测：
  - `server/tests/integration/server_smoke_test.cpp`
    - 覆盖 TCP 登录、建房、房间列表、加入房间、准备、开局、重连等链路。
  - `server/tests/integration/udp_sync_smoke_test.cpp`
    - 覆盖登录后发送 UDP 输入、等待 `S2C_GameStateDeltaSync`、校验 tick 连续与 delta 收敛。
  - 这两份测试验证的是“服务端协议行为”，不是“真实客户端代码路径”。
- 仓库已有一个基础网络工具：
  - `script/network_latency_check.sh`
    - 只做 ICMP ping 与延迟/丢包评估，可作为基础连通参考。
    - 它不能证明 TCP 登录、房间流、`session_token`、UDP endpoint 注册、同步包接收是否正常。
- 客户端目前没有任何测试目录或自动化联通入口，因此如果要“检查联通程度”，最缺的是：
  - 客户端可配置的 host/port 入口。
  - 一个不依赖完整 UI 操作的真实客户端联通诊断入口。
  - 一个统一编排服务端 smoke 与客户端诊断的脚本。

## Plan

1. 抽离客户端网络目标配置
   - 修改 `client/core/src/main/java/com/lawnmower/Config.java`，把服务器 host、TCP 端口、UDP 端口从硬编码常量改为“可配置 + 默认值回退”。
   - 配置来源优先采用最小侵入方案，例如 JVM 参数、环境变量或落地的本地配置文件。
   - 同步更新 `client/core/src/main/java/com/lawnmower/Main.java`，确保 TCP 与 UDP 初始化都从同一套配置读取。

2. 新增客户端真实链路诊断入口
   - 复用 `client/core/src/main/java/com/lawnmower/network/TcpClient.java` 与 `client/core/src/main/java/com/lawnmower/network/UdpClient.java`。
   - 新增一个独立诊断类，放在客户端网络相关包中，执行顺序对齐现有真实时序：
     - TCP connect
     - `C2S_Login`
     - 创建房间或加入房间
     - `C2S_StartGame`
     - 初始化 UDP
     - 发送带 `session_token` 的 UDP hello / player input
     - 等待 `S2C_GameStart` 与至少一类 `GameStateSync` / `GameStateDeltaSync`
   - 输出阶段化诊断结果，明确失败发生在 TCP 建连、登录、房间流、开局、UDP 注册还是同步接收阶段。

3. 为客户端诊断暴露可执行入口
   - 修改 `client/build.gradle`，增加一个可单独执行的 Gradle task 或独立 launcher，避免只能通过完整桌面游戏 UI 手工验证。
   - 尽量避免把诊断强耦合进 `DesktopLauncher` 或 `Main` 的 Screen 生命周期；如需复用公共逻辑，优先抽纯网络辅助方法。

4. 编排统一联通检查脚本
   - 新增 `script/check_connectivity.sh` 一类的仓库级脚本。
   - 流程建议为：
     - 构建服务端
     - 启动服务端（使用临时工作目录或临时配置，避免污染默认环境）
     - 运行 `server_smoke_test`
     - 运行 `udp_sync_smoke_test`
     - 运行客户端真实链路诊断
     - 汇总结果并返回退出码
   - 将 `script/network_latency_check.sh` 保留为“基础网络质量检查”的补充步骤，而不是协议联通主判据。

5. 修正文档与运行说明
   - 更新 `README.md`：
     - 删除或落实不存在的 `client_config.json` 说明。
     - 说明新的客户端目标地址配置方式。
     - 说明统一联通检查脚本与其覆盖范围。
   - 必要时补充 `AI_GUIDE.md` 的后续实施记录，确保后续执行阶段有审计上下文。

6. 实施时优先修改/新增的文件
   - `client/core/src/main/java/com/lawnmower/Config.java`
   - `client/core/src/main/java/com/lawnmower/Main.java`
   - `client/core/src/main/java/com/lawnmower/network/TcpClient.java`
   - `client/core/src/main/java/com/lawnmower/network/UdpClient.java`
   - `client/build.gradle`
   - `script/check_connectivity.sh`（新建）
   - `README.md`
   - 可能新增一个客户端诊断类与可执行 launcher

## Risks

- 客户端当前网络生命周期与 LibGDX 主线程、`Screen` 切换存在耦合，若直接把诊断塞进 `Main.java`，容易引入线程/渲染副作用。
- UDP 联通并不是单纯“端口能发包”：
  - 当前实现要求先完成 TCP 登录获得 `session_token`。
  - 还要求在开局后发送 `C2S_PlayerInput` 才会注册服务端 UDP endpoint。
  - 若诊断时序不严格对齐真实流程，容易误报“UDP 不通”。
- `README.md` 与代码现状不一致，说明此前文档可信度有限；实施时必须继续以代码与命令结果为准。
- 若客户端诊断仍依赖桌面窗口或完整 UI，自动化价值会很低，且在无图形环境下难以复现。
- 现有服务端 smoke tests 使用测试内自建客户端，因此它们能证明服务端协议大体可用，但不能替代真实 Java 客户端联通验证。
