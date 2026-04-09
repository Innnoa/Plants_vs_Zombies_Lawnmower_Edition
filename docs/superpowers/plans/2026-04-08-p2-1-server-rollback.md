# P2-1 Server Rollback Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** 完成服务器侧 P2-1 最小闭环：窗口内迟到输入可补洞，确认序号不跨缺口，补齐后服务端可基于历史重放修正当前权威位置/朝向并通过现有同步链路下发纠偏。

**Architecture:** 在 `PlayerRuntime` 内拆分“最高收到序号”和“连续确认序号”，增加窗口内输入缓存与已处理输入片段历史。tick 处理阶段只确认连续前缀；若迟到输入补齐缺口，则从最近历史快照开始重放该玩家输入片段，必要时修正当前权威状态并触发强制同步。

**Tech Stack:** C++20, Asio, Protobuf, CMake, ctest

---

### Task 1: Add Failing Integration Test

**Files:**
- Create: `server/tests/integration/late_input_reconciliation_smoke_test.cpp`
- Modify: `server/CMakeLists.txt`

- [ ] **Step 1: Write the failing test**

写一个单玩家 integration smoke：

- 配置安静房间，避免敌人与战斗干扰
- 连续发送 `seq=2`、`seq=3` 的移动输入
- 观察服务器同步，断言 `last_processed_input_seq` 不应越过缺口到 2/3
- 再发送迟到的 `seq=1`
- 继续观察同步，断言：
  - `last_processed_input_seq` 最终推进到 3
  - 玩家权威位置明显大于“只应用 seq2/3”时的位置，说明发生了补洞后的重放纠偏

- [ ] **Step 2: Run test to verify it fails**

Run: `env CCACHE_DISABLE=1 script/build_server.sh --debug && ctest --test-dir server/build-debug --output-on-failure -R late_input_reconciliation_smoke`

Expected: FAIL，原因应为当前服务器会直接拒绝迟到旧输入，或者错误确认更高序号。

- [ ] **Step 3: Register test target**

在 `server/CMakeLists.txt` 增加：

- `add_executable(late_input_reconciliation_smoke_test ...)`
- `target_link_libraries(...)`
- `add_test(NAME late_input_reconciliation_smoke ...)`

- [ ] **Step 4: Run test again to verify registered failure**

Run: `env CCACHE_DISABLE=1 script/build_server.sh --debug && ctest --test-dir server/build-debug --output-on-failure -R late_input_reconciliation_smoke`

Expected: FAIL with runtime assertion from the new smoke test.

### Task 2: Extend Player Runtime For Ordered Intake And Replay

**Files:**
- Modify: `server/include/game/managers/internal/game_manager_private_types.inc`
- Modify: `server/include/game/managers/game_manager.hpp`

- [ ] **Step 1: Add runtime structs**

给 `PlayerRuntime` 增加：

- `last_contiguous_input_seq`
- `highest_received_input_seq`
- `needs_prediction_reconciliation`
- `prediction_reconcile_from_tick`
- `processed_input_segments`
- `received_input_buffer`

每个 segment 至少包含：

- `input_seq`
- `input_tick`
- `applied_tick`
- `delta_seconds`
- `move_direction_x`
- `move_direction_y`

- [ ] **Step 2: Add method declarations**

在 `game_manager.hpp` 私有方法区声明：

- 输入缓存修剪
- 连续确认推进
- 已处理输入片段记录
- 单玩家回放重建
- tick 内纠偏执行

- [ ] **Step 3: Build to verify declarations compile**

Run: `env CCACHE_DISABLE=1 script/build_server.sh --debug`

Expected: FAIL，未实现新声明或调用链未接好。

### Task 3: Change Input Intake Semantics

**Files:**
- Modify: `server/src/game/managers/game_manager_session.cpp`

- [ ] **Step 1: Accept late inputs within window**

修改 `HandlePlayerInput`：

- 超窗旧输入继续拒绝
- 小于等于 `last_contiguous_input_seq` 的输入拒绝
- 重复收到的序号拒绝
- 合法但乱序的输入进入 `received_input_buffer` 与 `pending_inputs`
- 迟到补洞输入到达时标记需要纠偏

- [ ] **Step 2: Stop using last_input_seq as contiguous confirmation**

把 `last_input_seq` 语义收窄为“最高已收到/已处理序号”，不再直接代表可确认给客户端的连续前缀。

- [ ] **Step 3: Run failing test**

Run: `env CCACHE_DISABLE=1 script/build_server.sh --debug && ctest --test-dir server/build-debug --output-on-failure -R late_input_reconciliation_smoke`

Expected: 仍 FAIL，但 failure 应收敛为“位置未纠偏”或“确认序号推进逻辑仍不正确”。

### Task 4: Record Processed Segments And Confirm Only Contiguous Prefix

**Files:**
- Modify: `server/src/game/managers/game_manager_tick_flow.cpp`
- Modify: `server/src/game/managers/game_manager_runtime.cpp`
- Modify: `server/src/game/managers/game_manager_sync.cpp`

- [ ] **Step 1: Record processed input segments**

在 tick 内消费玩家输入时，记录每次实际消费的输入片段，包括：

- 应用到哪个 tick
- 实际消费的 `delta_seconds`
- 移动方向
- 对应 `input_seq/input_tick`

- [ ] **Step 2: Maintain contiguous confirmation frontier**

基于已收到窗口推进 `last_contiguous_input_seq`，同步消息中 `last_processed_input_seq` 改为该值。

- [ ] **Step 3: Trim old buffers with history window**

历史快照、输入缓存、片段历史统一受 `prediction_history_seconds` 控制。

- [ ] **Step 4: Re-run failing test**

Run: `env CCACHE_DISABLE=1 script/build_server.sh --debug && ctest --test-dir server/build-debug --output-on-failure -R late_input_reconciliation_smoke`

Expected: 仍 FAIL，但 failure 应只剩“没有位置纠偏”。

### Task 5: Implement Single-Player Replay Reconciliation

**Files:**
- Modify: `server/src/game/managers/game_manager_runtime.cpp`
- Modify: `server/src/game/managers/game_manager_tick_flow.cpp`
- Modify: `server/src/game/managers/game_manager_sync.cpp`

- [ ] **Step 1: Reconstruct from nearest pre-gap snapshot**

实现单玩家回放：

- 找到 `tick < affected_tick` 的最近快照
- 以快照位置/朝向为起点
- 按 `(applied_tick, input_seq)` 顺序重放片段

- [ ] **Step 2: Apply correction conservatively**

如果重放结果与当前玩家状态偏差超过阈值：

- 修正当前位置/朝向
- 更新时间威权 tick
- 标记玩家 dirty
- 增加短期强制同步窗口

若缺少可用快照或片段不完整：

- 不猜测重放
- 仅触发强制同步

- [ ] **Step 3: Run target test to green**

Run: `env CCACHE_DISABLE=1 script/build_server.sh --debug && ctest --test-dir server/build-debug --output-on-failure -R late_input_reconciliation_smoke`

Expected: PASS

### Task 6: Add Guard Tests For Reject Paths

**Files:**
- Modify: `server/tests/integration/late_input_reconciliation_smoke_test.cpp`

- [ ] **Step 1: Add duplicate/expired coverage**

在同一个 smoke 或追加第二个 smoke 覆盖：

- 重复旧输入不会重复进入处理链
- 超窗旧输入仍被拒绝

- [ ] **Step 2: Verify new assertions fail if implementation regresses**

Run: `ctest --test-dir server/build-debug --output-on-failure -R late_input_reconciliation_smoke`

Expected: PASS after implementation; assertions should be specific enough to catch regression.

### Task 7: Regression Verification

**Files:**
- None

- [ ] **Step 1: Build**

Run: `env CCACHE_DISABLE=1 script/build_server.sh --debug`

Expected: PASS

- [ ] **Step 2: Run focused regression tests**

Run: `ctest --test-dir server/build-debug --output-on-failure -R "late_input_reconciliation_smoke|udp_sync_smoke|server_smoke|tick_dispatch_gate"`

Expected: PASS

- [ ] **Step 3: Record receipts**

更新：

- `outputs/runtime/vibe-sessions/20260408-113648-p2-1-server-rollback/stage-lineage.json`
- `outputs/runtime/vibe-sessions/20260408-113648-p2-1-server-rollback/phase-plan_execute.json`
- `outputs/runtime/vibe-sessions/20260408-113648-p2-1-server-rollback/cleanup-receipt.json`
