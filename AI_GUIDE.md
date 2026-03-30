# AI Guide

<!-- AI_GUIDE:SUMMARY:BEGIN -->
## 0. 文件说明

本文件用于记录当前仓库的 AI 审计摘要、近期执行记录、修改轨迹与后续待办。
只记录必要的技术结论与验证信息，不记录密码、令牌、密钥或其他敏感内容。

## 1. 当前项目状态

- 项目根目录：`/home/zazaki/Projects/PVZ/.agtx/worktrees/de24f340-检查代码结构`
- 主要结构：`server/` C++20 权威服务器，`client/` Java 17 LibGDX 客户端，`proto/` 协议定义，`game_config/` 运行配置
- 服务端构建入口：`script/build_server.sh`、`server/CMakeLists.txt`
- 服务端测试入口：`ctest --test-dir server/build-debug --output-on-failure`
- 当前阶段：服务器结构检查已完成，当前产物以文档审计与路径校正为主
- 已知限制：`GameManager` 仍通过多个 `.cpp`、`.inc` 与私有头组合实现，理解运行链路时需要交叉查看多个文件

## 2. 当前约定与风险

- 结构判断以真实代码、构建脚本和测试入口为准，不能只依赖旧文档。
- 服务端职责拆分集中在 `server/src/network/*` 与 `server/src/game/managers/*`，其中 `GameManager` 通过多个 `.cpp`、`internal/*.hpp`、`.inc` 文件协作，边界不宜凭文件名臆测。
- `README.md` 的项目结构描述与当前仓库实际目录可能存在偏差，后续如执行结构梳理需要做一致性校验。

## 3. 最近一次会话摘要

- 2026-03-30：按 `agtx-execute` 技能完成“检查当前服务器结构”的执行，新增 `server/docs/server_structure_audit.md`，修正 `README.md` 与 `server/docs/AI_GUIDE.md` 中的结构漂移说明，并完成 `script/build_server.sh --debug` 与全部 smoke tests 验证。
<!-- AI_GUIDE:SUMMARY:END -->

## 4. 执行记录

<!-- AI_GUIDE:ENTRY:BEGIN ts=2026-03-30 15:21:13 CST id=session-20260330-152113-structure-plan -->
- 目标：检查当前服务器结构，并输出可执行的后续计划。
- 操作：读取 `server/docs/AI_GUIDE.md`、`README.md`、`server/CMakeLists.txt`、`script/build_server.sh`、`server/src/main.cpp` 以及网络层/管理器头文件与 smoke test。
- 涉及文件：`server/docs/AI_GUIDE.md`、`README.md`、`server/CMakeLists.txt`、`script/build_server.sh`、`server/src/main.cpp`、`server/include/network/tcp/tcp_session.hpp`、`server/include/network/udp/udp_server.hpp`、`server/include/game/managers/game_manager.hpp`、`server/include/game/managers/room_manager.hpp`。
- 验证：仅进行了静态结构扫描与目录核对，未运行构建或测试命令。
- 结论：当前服务端为“配置加载 + TCP/UDP 网络层 + RoomManager/GameManager 权威逻辑 + smoke tests”的结构；计划已写入 `.agtx/plan.md`。
<!-- AI_GUIDE:ENTRY:END ts=2026-03-30 15:21:13 CST id=session-20260330-152113-structure-plan -->

<!-- AI_GUIDE:ENTRY:BEGIN ts=2026-03-30 15:38:53 CST id=session-20260330-153853-structure-execute -->
- 目标：按已批准计划输出服务端结构审计结果，并校正文档漂移。
- 操作：新增 `server/docs/server_structure_audit.md`；修正 `README.md` 中的项目结构、proto 文件名与服务端测试命令；修正 `server/docs/AI_GUIDE.md` 中的 smoke test 路径与 manager 私有头位置说明。
- 涉及文件：`server/docs/server_structure_audit.md`、`README.md`、`server/docs/AI_GUIDE.md`、`AI_GUIDE.md`。
- 验证：`script/build_server.sh --configure-only` 成功；`script/build_server.sh --debug` 成功；`ctest --test-dir server/build-debug --output-on-failure` 通过 3/3。首次无提权执行 `build` 与 `ctest` 分别受 `~/.cache/ccache` 写权限和本地 socket 限制影响，提权重跑后通过。
- 结论：当前服务端结构审计已形成独立文档，并与现有 README / 服务端 AI 指南对齐。
<!-- AI_GUIDE:ENTRY:END ts=2026-03-30 15:38:53 CST id=session-20260330-153853-structure-execute -->

## 5. 修改标记 / 审计轨迹

<!-- AI_GUIDE:TRACE:BEGIN ts=2026-03-30 15:21:13 CST id=session-20260330-152113-structure-plan -->
- 新建仓库根级 `AI_GUIDE.md`，作为本工作树的审计入口。
- 参考现有 `server/docs/AI_GUIDE.md` 补充服务端背景，但本轮结论以代码扫描结果为准。
- 规划产物将写入 `.agtx/plan.md`，后续若执行结构梳理，优先新增或更新服务端文档，而非直接改动业务逻辑。
<!-- AI_GUIDE:TRACE:END ts=2026-03-30 15:21:13 CST id=session-20260330-152113-structure-plan -->

<!-- AI_GUIDE:TRACE:BEGIN ts=2026-03-30 15:38:53 CST id=session-20260330-153853-structure-execute -->
- 新增 `server/docs/server_structure_audit.md`，作为当前服务端结构快照。
- 更新 `README.md` 中与当前服务器结构直接相关的失真内容，避免继续引用旧路径。
- 更新 `server/docs/AI_GUIDE.md` 的测试路径与私有头说明，使其与真实目录一致。
<!-- AI_GUIDE:TRACE:END ts=2026-03-30 15:38:53 CST id=session-20260330-153853-structure-execute -->

## 6. 待办与后续动作

- 若后续继续拆分 `GameManager`，优先补充更细的 tick / 同步链路说明文档。
- 若服务端测试布局或构建目录约定再次变化，需要同步更新 `README.md`、`server/docs/AI_GUIDE.md` 与 `server/docs/server_structure_audit.md`。

<!-- AI_GUIDE:READ_ANCHOR ts=2026-03-30 15:38:53 CST id=session-20260330-153853-structure-execute -->
