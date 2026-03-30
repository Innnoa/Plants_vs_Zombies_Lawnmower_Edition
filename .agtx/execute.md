## Changes

- `client/core/src/main/java/com/lawnmower/Config.java`
  - 将服务器 host / TCP 端口 / UDP 端口改为可配置来源。
  - 支持通过环境变量 `LAWNMOWER_SERVER_HOST`、`LAWNMOWER_SERVER_TCP_PORT`、`LAWNMOWER_SERVER_UDP_PORT` 或 JVM 参数 `lawnmower.server.*` 覆盖默认值。
  - 保留原有默认目标，避免影响现有手工运行路径。

- `client/core/src/main/java/com/lawnmower/network/ConnectivityCheck.java`
  - 新增独立客户端联通诊断入口。
  - 复用现有 `TcpClient` / `UdpClient`，按真实时序执行 TCP connect、登录、建房、开局、UDP 注册与状态同步验证。
  - 输出阶段化结果，明确 TCP 与 UDP 检查是否通过。

- `client/build.gradle`
  - 为 `core` 模块新增 `runConnectivityCheck` 与 `printConnectivityCheckClasspath` 任务。
  - 为 `core` / `desktop` 的 `JavaExec` 任务透传 `lawnmower.server.*` 与 `lawnmower.connectivity.timeoutMs` JVM 参数。
  - 保留桌面模块的联通诊断入口，但脚本执行路径改为更轻量的 `core` 任务与直接 `java -cp`。

- `client/gradle/wrapper/gradle-wrapper.properties`
  - 将 `networkTimeout` 从 `10000` 提高到 `120000`，降低首次拉取 Gradle 分发时的误判失败概率。

- `script/check_connectivity.sh`
  - 新增一键联通检查脚本。
  - 自动执行服务端构建、`server_smoke`、`udp_sync_smoke`、本地临时服务端启动与客户端真实链路诊断。
  - 未显式指定端口时自动挑选空闲 TCP/UDP 端口，避免默认端口冲突。
  - 优先复用可写的 `~/.gradle` / `~/.cache/ccache`，仅在不可写时回退到 `/tmp`。
  - 诊断执行路径改为：Gradle 仅负责编译 `core` 与输出 runtime classpath，最终由 `java -cp` 直接启动 `ConnectivityCheck`。

- `README.md`
  - 更新网络测试章节，改为仓库内真实存在且已验证的 `script/check_connectivity.sh` 与 `script/network_latency_check.sh`。
  - 修正文档中不存在的 `client_config.json` 说明，改为环境变量 / JVM 参数配置方式。
  - 将客户端 Gradle 示例统一改成 `bash ./gradlew ...`，匹配当前仓库中文件权限现状。

- `AI_GUIDE.md`
  - 记录本轮执行阶段的实现与验证结果，补充最新读取锚点与审计轨迹。

## Testing

- `bash -n script/check_connectivity.sh`
  - 结果：通过。

- `./script/check_connectivity.sh`
  - 运行方式：在可打开本地 TCP/UDP socket 的环境中执行。
  - 最终结果：通过。
  - 关键信息：
    - `server_smoke` 通过
    - `udp_sync_smoke` 通过
    - 客户端诊断输出：
      - `TCP connect` 通过
      - `Login` 通过
      - `Create room` 通过
      - `Start game` 通过
      - `UDP sync` 通过，收到 `MSG_S2C_GAME_STATE_SYNC`

- 直接客户端诊断验证
  - 方式：启动临时服务端后，使用 `java -cp <runtime classpath> com.lawnmower.network.ConnectivityCheck`
  - 结果：通过。
  - 目的：在脚本调试阶段独立确认客户端真实 TCP/UDP 检查逻辑正确。

- 过程中修复的问题
  - `~/.cache/ccache` 在受限环境不可写：脚本增加可写目录回退逻辑。
  - `client/gradlew` 无执行权限：文档与脚本改为 `bash ./gradlew`。
  - Gradle wrapper 首次下载超时：提高 wrapper `networkTimeout`。
  - 默认 `7777/7778` 端口冲突：脚本改为自动选择空闲端口。
  - `desktop` 运行时依赖过重：脚本改为基于 `core` classpath 的直接 `java -cp` 诊断路径。
