# P2-2 Server Channel Routing Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** 为服务器建立独立的频道语义层，把 TCP/UDP 路由和 fallback/backlog/budget 规则收敛到统一策略表，而不修改外层协议或客户端。

**Architecture:** 新增 `channel_policy` 模块，定义频道枚举、传输策略和消息分类查询。状态同步、事件分发、输入上行和基础 TCP 发送入口统一先查询 policy，再由 policy 决定默认传输层、是否允许 fallback、是否受 backlog/budget 约束。

**Tech Stack:** C++20, Asio, Protobuf, CMake, ctest

---

### Task 1: Add Failing Policy Classification Test

**Files:**
- Create: `server/tests/unit/channel_policy_test.cpp`
- Modify: `server/CMakeLists.txt`
- Create: `server/include/network/channel_policy.hpp`

- [ ] **Step 1: Write the failing test**

写一个最小单元测试，断言以下映射存在：

- `MSG_C2S_PLAYER_INPUT -> UnreliableRealtimeState`
- `MSG_S2C_GAME_STATE_DELTA_SYNC -> UnreliableRealtimeState`
- `MSG_S2C_GAME_STATE_SYNC(full snapshot path) -> ReliableControl`
- `MSG_S2C_PLAYER_HURT_BATCH -> ReliableCriticalEvent`
- `MSG_S2C_GAME_OVER -> ReliableCriticalEvent`
- `MSG_S2C_UPGRADE_REQUEST -> ReliableControl`

测试骨架：

```cpp
Expect(ResolveMessageChannel(MSG_C2S_PLAYER_INPUT) ==
       MessageChannel::kUnreliableRealtimeState,
       "player input channel mismatch");
```

- [ ] **Step 2: Register the test target**

在 `server/CMakeLists.txt` 增加：

```cmake
add_executable(channel_policy_test
  ${TESTS_UNIT_DIR}/channel_policy_test.cpp
  src/network/channel_policy.cpp
)
target_include_directories(channel_policy_test PRIVATE include)
target_link_libraries(channel_policy_test PRIVATE proto_lib)

add_test(
  NAME channel_policy
  COMMAND channel_policy_test
)
```

- [ ] **Step 3: Run test to verify it fails**

Run: `env CCACHE_DISABLE=1 script/build_server.sh --debug && ctest --test-dir server/build-debug --output-on-failure -R channel_policy`

Expected: FAIL because `channel_policy` API is not implemented yet.

### Task 2: Create Channel Policy Module

**Files:**
- Create: `server/include/network/channel_policy.hpp`
- Create: `server/src/network/channel_policy.cpp`

- [ ] **Step 1: Define channel model**

在 `channel_policy.hpp` 定义：

```cpp
enum class MessageChannel {
  kReliableControl = 0,
  kReliableCriticalEvent = 1,
  kUnreliableRealtimeState = 2,
};

enum class DeliveryTransport {
  kTcpOnly = 0,
  kUdpOnly = 1,
  kUdpPreferredTcpFallback = 2,
};

enum class DeliveryScope {
  kSingleSession = 0,
  kRoomBroadcast = 1,
  kRoomBroadcastWithFallback = 2,
};

struct ChannelPolicy {
  MessageChannel channel = MessageChannel::kReliableControl;
  DeliveryTransport default_transport = DeliveryTransport::kTcpOnly;
  DeliveryScope delivery_scope = DeliveryScope::kSingleSession;
  bool allow_fallback = false;
  bool allow_backlog = false;
  bool subject_to_packet_budget = false;
  bool subject_to_event_entry_budget = false;
};

ChannelPolicy ResolveMessageChannelPolicy(lawnmower::MessageType type);
MessageChannel ResolveMessageChannel(lawnmower::MessageType type);
```

- [ ] **Step 2: Implement minimal classification**

在 `channel_policy.cpp` 先只覆盖当前第一阶段需要的消息：

```cpp
switch (type) {
  case lawnmower::MSG_C2S_PLAYER_INPUT:
  case lawnmower::MSG_S2C_GAME_STATE_DELTA_SYNC:
    return {/* realtime policy */};
  case lawnmower::MSG_S2C_PLAYER_HURT_BATCH:
  case lawnmower::MSG_S2C_ENEMY_DIED_BATCH:
  case lawnmower::MSG_S2C_PLAYER_LEVEL_UP_BATCH:
  case lawnmower::MSG_S2C_GAME_OVER:
    return {/* critical event policy */};
  default:
    return {/* conservative control policy */};
}
```

- [ ] **Step 3: Run unit test to verify it passes**

Run: `env CCACHE_DISABLE=1 script/build_server.sh --debug && ctest --test-dir server/build-debug --output-on-failure -R channel_policy`

Expected: PASS

### Task 3: Route State Sync Through Policy

**Files:**
- Modify: `server/src/game/managers/game_manager_sync_dispatch.cpp`
- Modify: `server/include/game/managers/internal/game_manager_sync_dispatch.hpp`
- Modify: `server/src/network/udp/udp_server.cpp`
- Modify: `server/include/network/udp/udp_server.hpp`
- Reference: `server/include/network/channel_policy.hpp`

- [ ] **Step 1: Replace ad-hoc state route enum with policy-derived route**

把当前 `StatePacketRoute` 的决策改为通过 `ResolveMessageChannelPolicy(...)` 获取，至少覆盖：

- `S2C_GameStateDeltaSync`
- 非 full snapshot 的 `S2C_GameStateSync`
- full snapshot 的 `S2C_GameStateSync`

- [ ] **Step 2: Preserve current semantics via policy**

确保：

- delta 仍是 `UdpPreferredTcpFallback`
- full snapshot 仍是 `TcpOnly`
- 非 full snapshot sync 仍按当前是否与 delta 同帧的条件落到原有逻辑

- [ ] **Step 3: Run focused regression**

Run: `env CCACHE_DISABLE=1 script/build_server.sh --debug && ctest --test-dir server/build-debug --output-on-failure -R "channel_policy|udp_sync_smoke"`

Expected: PASS

### Task 4: Route Event Dispatch Through Policy

**Files:**
- Modify: `server/src/game/managers/game_manager_event_dispatch.cpp`
- Reference: `server/include/network/channel_policy.hpp`

- [ ] **Step 1: Classify event packets via policy**

对以下消息在组包或发送前统一取 policy：

- `MSG_S2C_PLAYER_HURT_BATCH`
- `MSG_S2C_ENEMY_DIED_BATCH`
- `MSG_S2C_PLAYER_LEVEL_UP_BATCH`
- `MSG_S2C_UPGRADE_REQUEST`
- `MSG_S2C_GAME_OVER`
- 非关键事件：`PROJECTILE_SPAWN / PROJECTILE_DESPAWN / DROPPED_ITEM / ENEMY_ATTACK_STATE_SYNC`

- [ ] **Step 2: Use policy to mark backlog behavior**

关键事件：

```cpp
policy.allow_backlog == false;
policy.default_transport == DeliveryTransport::kTcpOnly;
```

非关键事件：

```cpp
policy.allow_backlog == true;
policy.subject_to_packet_budget == true;
policy.subject_to_event_entry_budget == true;
```

- [ ] **Step 3: Run regression**

Run: `env CCACHE_DISABLE=1 script/build_server.sh --debug && ctest --test-dir server/build-debug --output-on-failure -R "channel_policy|tcp_event_batch_smoke|server_smoke"`

Expected: PASS

### Task 5: Route Input Intake And Base Send Entry Through Policy

**Files:**
- Modify: `server/src/network/tcp/tcp_session.cpp`
- Modify: `server/src/network/udp/udp_server.cpp`
- Modify: `server/src/network/tcp/session_gameplay.cpp`
- Reference: `server/include/network/channel_policy.hpp`

- [ ] **Step 1: Add explicit policy lookup for player input**

在 TCP 和 UDP 输入入口都显式查询 `MSG_C2S_PLAYER_INPUT` policy，确保服务器内部不再靠注释或路径约定表达“这是 realtime input”。

- [ ] **Step 2: Add defensive logging for unexpected route mismatches**

例如：

```cpp
const auto policy = ResolveMessageChannelPolicy(lawnmower::MSG_C2S_PLAYER_INPUT);
if (policy.channel != MessageChannel::kUnreliableRealtimeState) {
  spdlog::warn("player input policy mismatch");
}
```

- [ ] **Step 3: Keep behavior stable**

不要改变：

- TCP fallback 现状
- UDP token 校验
- 登录/房间/重连等控制消息仍走 TCP

- [ ] **Step 4: Run focused regression**

Run: `env CCACHE_DISABLE=1 script/build_server.sh --debug && ctest --test-dir server/build-debug --output-on-failure -R "channel_policy|server_smoke|udp_sync_smoke|tcp_event_batch_smoke"`

Expected: PASS

### Task 6: Final Verification And Receipts

**Files:**
- Modify: `outputs/runtime/vibe-sessions/<current-run>/...`

- [ ] **Step 1: Full focused verification**

Run: `env CCACHE_DISABLE=1 script/build_server.sh --debug`

Expected: PASS

- [ ] **Step 2: Run channel-related regression set**

Run: `ctest --test-dir server/build-debug --output-on-failure -R "channel_policy|server_smoke|udp_sync_smoke|tcp_event_batch_smoke"`

Expected: PASS

- [ ] **Step 3: Record vibe receipts**

回写：

- `outputs/runtime/vibe-sessions/<run-id>/phase-plan_execute.json`
- `outputs/runtime/vibe-sessions/<run-id>/cleanup-receipt.json`

确保注明：

- 本轮只完成服务器内部频道层
- 未改协议外壳
- 未做可靠 UDP / 应用层重传
