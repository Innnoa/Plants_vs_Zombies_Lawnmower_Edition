# AI_GUIDE

<!-- AI_GUIDE:SUMMARY:BEGIN -->
## 0. 文件说明

- 本文件用于记录当前仓库内 AI 会话的精简审计信息。
- 每次会话优先读取本概要区和最近读取锚点后的增量记录。
- 禁止记录密码、令牌、密钥或其他敏感信息。

## 1. 当前项目状态

- 项目目录：`/home/zazaki/Projects/PVZ/.agtx/worktrees/4a174855-检查当前服务器与客户端联通程度`
- 技术栈：服务端为 C++20 + Asio + Protobuf + CMake；客户端为 Java 17 + LibGDX + Gradle + Protobuf
- 构建/测试入口：`script/build_server.sh --debug`；`ctest --test-dir server/build-debug --output-on-failure`；`bash client/gradlew -p client core:classes`；`./script/check_connectivity.sh`
- 当前阶段：执行完成，等待交付
- 已知限制：完整联通检查需要可打开本地 TCP/UDP socket 的运行环境；受限沙箱中执行 `./script/check_connectivity.sh` 可能需要额外授权

## 2. 当前约定与风险

- 结论必须基于仓库内代码、配置、命令结果或文档证据。
- 当前任务目标为“检查当前服务器与客户端联通程度”，重点覆盖 TCP 登录/房间流与开局后的 UDP 输入/状态同步。
- `script/network_latency_check.sh` 只能证明基础网络质量，不能替代应用协议联通检查。
- `script/check_connectivity.sh` 已成为当前真实客户端链路检查入口，但默认依赖本地端口绑定与临时服务端进程启动能力。
- 客户端网络目标现已支持环境变量与 JVM 参数覆盖，默认值仍保持旧地址，避免影响既有手工运行路径。

## 3. 最近一次会话摘要

- 已完成“检查当前服务器与客户端联通程度”的执行阶段实现。
- 已为客户端补齐可配置服务器目标、独立联通诊断入口和一键检查脚本，并同步修正文档与 Gradle 入口。
- 已验证 `bash -n script/check_connectivity.sh` 通过，且在可用本地 socket 的环境中执行 `./script/check_connectivity.sh` 全链路通过，覆盖 TCP 登录/建房/开局与 UDP 状态同步。
- 已调整项目内 `.agtx/skills` 的 `research / plan / execute / review` 模板，使其更符合全局 `AGENTS.md` 中关于 `AGTX_CONTEXT`、证据优先、验证输出和非递归委派的约束。
<!-- AI_GUIDE:SUMMARY:END -->

## 4. 执行记录

<!-- AI_GUIDE:ENTRY:BEGIN ts=2026-03-30 15:26:27 CST id=session-20260330-152627-connectivity-plan -->
- 时间：`2026-03-30 15:26:27 CST`
- 类型：仅规划
- 目标：为“检查当前服务器与客户端联通程度”生成实施计划，不修改业务源码。
- 关键读取：
  - `server/docs/AI_GUIDE.md`
  - `README.md`
  - `game_config/server_config.json`
  - `server/src/main.cpp`
  - `server/tests/integration/server_smoke_test.cpp`
  - `server/tests/integration/udp_sync_smoke_test.cpp`
  - `client/core/src/main/java/com/lawnmower/Config.java`
  - `client/core/src/main/java/com/lawnmower/Main.java`
  - `client/core/src/main/java/com/lawnmower/network/TcpClient.java`
  - `client/core/src/main/java/com/lawnmower/network/UdpClient.java`
  - `client/build.gradle`
  - `script/network_latency_check.sh`
- 结论：
  - 服务端已有 TCP/UDP 协议级 smoke tests，可作为对照验证。
  - 客户端真实链路缺少自动化联通检查，且目标地址配置目前硬编码。
  - 实施计划已写入 `.agtx/plan.md`，建议后续优先补齐客户端配置入口、诊断入口和统一检查脚本。
- 验证：本轮为规划会话，未执行构建或测试。
<!-- AI_GUIDE:ENTRY:END ts=2026-03-30 15:26:27 CST id=session-20260330-152627-connectivity-plan -->

<!-- AI_GUIDE:ENTRY:BEGIN ts=2026-03-30 17:11:23 CST id=session-20260330-171123-connectivity-execute -->
- 时间：`2026-03-30 17:11:23 CST`
- 类型：执行
- 目标：按 `.agtx/plan.md` 完成当前服务器与客户端联通检查链路，实现真实 Java 客户端的自动化 TCP/UDP 诊断。
- 关键修改：
  - `client/core/src/main/java/com/lawnmower/Config.java`
  - `client/core/src/main/java/com/lawnmower/network/ConnectivityCheck.java`
  - `client/build.gradle`
  - `client/gradle/wrapper/gradle-wrapper.properties`
  - `script/check_connectivity.sh`
  - `README.md`
  - `.agtx/execute.md`
- 关键结果：
  - 客户端服务器 host / TCP / UDP 端口支持环境变量与 JVM 参数覆盖。
  - 新增独立 `ConnectivityCheck`，复用现有 `TcpClient` / `UdpClient`，按真实时序验证 TCP 登录、建房、开局、UDP 注册与状态同步。
  - 新增 `script/check_connectivity.sh`，统一执行服务端构建、`server_smoke`、`udp_sync_smoke`、临时服务端启动与客户端真实链路检查，并在未指定端口时自动分配空闲端口。
  - README 已改为仓库内真实可用的配置方式与检查命令。
- 验证：
  - `bash -n script/check_connectivity.sh`：通过。
  - `./script/check_connectivity.sh`：通过；`server_smoke`、`udp_sync_smoke`、客户端 `TCP connect`、`Login`、`Create room`、`Start game`、`UDP sync` 均通过，并输出“联通检查完成。”。
  - `java -cp <runtime classpath> com.lawnmower.network.ConnectivityCheck`：在临时服务端环境下单独验证通过。
- 结论：
  - 当前仓库已具备真实客户端到本地服务端的端到端联通检查能力。
- 沙箱环境若限制本地 socket 建立，则执行最终联通脚本时仍需在允许本地网络绑定的环境中运行。
<!-- AI_GUIDE:ENTRY:END ts=2026-03-30 17:11:23 CST id=session-20260330-171123-connectivity-execute -->

<!-- AI_GUIDE:ENTRY:BEGIN ts=2026-04-03 10:24:59 CST id=session-20260403-102459-agtx-skill-tuning -->
- 时间：`2026-04-03 10:24:59 CST`
- 类型：流程模板调整
- 目标：让当前项目的 `agtx` 阶段技能模板更符合全局 `AGENTS.md` 的 `AGTX_CONTEXT`、状态源、验证和委派边界要求。
- 关键读取：
  - `/home/zazaki/.codex/AGENTS.md`
  - `/home/zazaki/.codex/RUNBOOK_FULL.md`
  - `.agtx/skills/agtx-research/SKILL.md`
  - `.agtx/skills/agtx-plan/SKILL.md`
  - `.agtx/skills/agtx-execute/SKILL.md`
  - `.agtx/skills/agtx-review/SKILL.md`
- 关键修改：
  - 为 `research / plan / execute / review` 四个阶段模板补充 `AGTX_CONTEXT=true` 约束说明。
  - 明确 `.agtx/*` 是任务主状态源，`AI_GUIDE.md / DECISIONS.md` 仅作最小长期上下文补读。
  - 在模板中加入“证据优先、不得猜测、默认不递归使用 subagent、输出需包含验证与剩余风险”等要求。
  - 调整 `review` 阶段输出结构，使其以 findings 为先，更贴近全局 review 规则。
- 验证：
  - 已逐文件复核四个模板，确认保留原有 phase 语义与 `stop and wait` 行为。
  - 本轮未运行业务构建或测试；变更仅影响后续 `agtx` 任务的提示词与阶段输出结构。
- 结论：
  - 当前项目内后续由这些模板生成的 `agtx` 阶段会话，将更稳定地遵循全局规则，而不是只依赖会话侧隐式指令。
<!-- AI_GUIDE:ENTRY:END ts=2026-04-03 10:24:59 CST id=session-20260403-102459-agtx-skill-tuning -->

## 5. 修改标记 / 审计轨迹

<!-- AI_GUIDE:TRACE:BEGIN ts=2026-03-30 15:26:27 CST id=trace-20260330-152627-connectivity-plan -->
- 文件：`AI_GUIDE.md`
- 动作：初始化并补充本轮规划审计摘要、执行记录与读取锚点
- 原因：仓库根此前缺少统一审计主文件，需要为后续执行阶段提供连续上下文
<!-- AI_GUIDE:TRACE:END ts=2026-03-30 15:26:27 CST id=trace-20260330-152627-connectivity-plan -->

<!-- AI_GUIDE:TRACE:BEGIN ts=2026-03-30 15:26:27 CST id=trace-20260330-152627-plan-file -->
- 文件：`.agtx/plan.md`
- 动作：新增实施计划
- 原因：用户通过 `agtx-plan` 请求为“检查当前服务器与客户端联通程度”生成计划
<!-- AI_GUIDE:TRACE:END ts=2026-03-30 15:26:27 CST id=trace-20260330-152627-plan-file -->

<!-- AI_GUIDE:TRACE:BEGIN ts=2026-03-30 17:11:23 CST id=trace-20260330-171123-config -->
- 文件：`client/core/src/main/java/com/lawnmower/Config.java`
- 动作：扩展服务器 host / TCP / UDP 端口读取逻辑，支持环境变量与 JVM 参数覆盖
- 原因：解除客户端网络目标硬编码限制，便于联通检查在临时服务端和动态端口下运行
<!-- AI_GUIDE:TRACE:END ts=2026-03-30 17:11:23 CST id=trace-20260330-171123-config -->

<!-- AI_GUIDE:TRACE:BEGIN ts=2026-03-30 17:11:23 CST id=trace-20260330-171123-connectivity-check -->
- 文件：`client/core/src/main/java/com/lawnmower/network/ConnectivityCheck.java`
- 动作：新增独立客户端联通诊断入口
- 原因：为真实 Java 客户端补齐可自动执行的 TCP/UDP 协议级联通验证能力
<!-- AI_GUIDE:TRACE:END ts=2026-03-30 17:11:23 CST id=trace-20260330-171123-connectivity-check -->

<!-- AI_GUIDE:TRACE:BEGIN ts=2026-03-30 17:11:23 CST id=trace-20260330-171123-build-gradle -->
- 文件：`client/build.gradle`
- 动作：新增联通诊断相关 Gradle 任务并透传网络相关 JVM 参数
- 原因：为脚本与手动调试提供稳定的客户端编译与 classpath 输出入口
<!-- AI_GUIDE:TRACE:END ts=2026-03-30 17:11:23 CST id=trace-20260330-171123-build-gradle -->

<!-- AI_GUIDE:TRACE:BEGIN ts=2026-03-30 17:11:23 CST id=trace-20260330-171123-wrapper-timeout -->
- 文件：`client/gradle/wrapper/gradle-wrapper.properties`
- 动作：提高 Gradle wrapper 网络超时时间
- 原因：降低首次下载分发包时的误判失败概率，减少联通检查脚本的不稳定性
<!-- AI_GUIDE:TRACE:END ts=2026-03-30 17:11:23 CST id=trace-20260330-171123-wrapper-timeout -->

<!-- AI_GUIDE:TRACE:BEGIN ts=2026-03-30 17:11:23 CST id=trace-20260330-171123-check-script -->
- 文件：`script/check_connectivity.sh`
- 动作：新增一键联通检查脚本，串联服务端 smoke tests、临时服务端启动与客户端真实链路检查
- 原因：将 TCP/UDP 联通验证沉淀为可重复执行的统一入口，并自动规避端口冲突
<!-- AI_GUIDE:TRACE:END ts=2026-03-30 17:11:23 CST id=trace-20260330-171123-check-script -->

<!-- AI_GUIDE:TRACE:BEGIN ts=2026-03-30 17:11:23 CST id=trace-20260330-171123-readme -->
- 文件：`README.md`
- 动作：更新客户端配置与网络检查文档
- 原因：移除仓库中不存在的配置说明，改为真实可用的环境变量、JVM 参数与脚本入口
<!-- AI_GUIDE:TRACE:END ts=2026-03-30 17:11:23 CST id=trace-20260330-171123-readme -->

<!-- AI_GUIDE:TRACE:BEGIN ts=2026-03-30 17:11:23 CST id=trace-20260330-171123-execute-summary -->
- 文件：`.agtx/execute.md`
- 动作：写入执行阶段变更说明与验证结果
- 原因：按 `agtx-execute` 技能要求输出实现总结，供用户审批与后续审阅
<!-- AI_GUIDE:TRACE:END ts=2026-03-30 17:11:23 CST id=trace-20260330-171123-execute-summary -->

<!-- AI_GUIDE:TRACE:BEGIN ts=2026-04-03 10:24:59 CST id=trace-20260403-102459-research-skill -->
- 文件：`.agtx/skills/agtx-research/SKILL.md`
- 动作：增强研究阶段模板，补充 `AGTX_CONTEXT`、证据要求与非递归委派边界
- 原因：让 research 阶段优先基于 `.agtx/*` 与代码证据产出结论，而不是依赖猜测或隐式上下文
<!-- AI_GUIDE:TRACE:END ts=2026-04-03 10:24:59 CST id=trace-20260403-102459-research-skill -->

<!-- AI_GUIDE:TRACE:BEGIN ts=2026-04-03 10:24:59 CST id=trace-20260403-102459-plan-skill -->
- 文件：`.agtx/skills/agtx-plan/SKILL.md`
- 动作：增强计划阶段模板，加入验证设计、开放问题和 `AGTX_CONTEXT` 约束
- 原因：让 planning 阶段的计划输出更贴近全局规则中的验证与风险表达要求
<!-- AI_GUIDE:TRACE:END ts=2026-04-03 10:24:59 CST id=trace-20260403-102459-plan-skill -->

<!-- AI_GUIDE:TRACE:BEGIN ts=2026-04-03 10:24:59 CST id=trace-20260403-102459-execute-skill -->
- 文件：`.agtx/skills/agtx-execute/SKILL.md`
- 动作：增强执行阶段模板，明确以 `.agtx/plan.md` 为批准范围，并输出偏差与剩余风险
- 原因：降低执行阶段脱离已批准计划或遗漏验证/风险信息的概率
<!-- AI_GUIDE:TRACE:END ts=2026-04-03 10:24:59 CST id=trace-20260403-102459-execute-skill -->

<!-- AI_GUIDE:TRACE:BEGIN ts=2026-04-03 10:24:59 CST id=trace-20260403-102459-review-skill -->
- 文件：`.agtx/skills/agtx-review/SKILL.md`
- 动作：增强 review 阶段模板，要求 findings 优先输出，并补充修复与残余风险结构
- 原因：让 review 阶段更贴近全局规则中“review 以问题为主”的交付风格
<!-- AI_GUIDE:TRACE:END ts=2026-04-03 10:24:59 CST id=trace-20260403-102459-review-skill -->

<!-- AI_GUIDE:TRACE:BEGIN ts=2026-04-03 10:24:59 CST id=trace-20260403-102459-ai-guide -->
- 文件：`AI_GUIDE.md`
- 动作：追加本轮 `agtx` 技能模板调整的审计记录与读取锚点
- 原因：为后续会话保留“为什么该项目的 agtx 模板行为发生变化”的可追溯上下文
<!-- AI_GUIDE:TRACE:END ts=2026-04-03 10:24:59 CST id=trace-20260403-102459-ai-guide -->

## 6. 待办与后续动作

- 当前计划范围内事项已完成。
- 后续如需对远端或跨主机环境做联通验证，可直接基于 `LAWNMOWER_SERVER_*` 环境变量或 `lawnmower.server.*` JVM 参数复用现有检查入口。

<!-- AI_GUIDE:READ_ANCHOR ts=2026-03-30 00:00:00 CST id=session-20260330-000000-init -->
<!-- AI_GUIDE:READ_ANCHOR ts=2026-03-30 15:26:27 CST id=session-20260330-152627-connectivity-plan -->
<!-- AI_GUIDE:READ_ANCHOR ts=2026-03-30 17:11:23 CST id=session-20260330-171123-connectivity-execute -->
<!-- AI_GUIDE:READ_ANCHOR ts=2026-04-03 10:24:59 CST id=session-20260403-102459-agtx-skill-tuning -->
