# 2026-04-08 P2-1 Server Rollback Execution Plan

## Internal Grade

`XL`

说明：

- 这是多阶段实现，但当前没有获得用户明确的多代理授权，所以采用 `XL` 的波次化串行执行，不使用 `spawn_agent`。

## Waves

### Wave 1: Freeze Runtime Context

- 写入治理产物
- 明确只做服务器-only 的 `A` 方案

### Wave 2: Red Tests

- 新增迟到输入补洞测试
- 先观察失败，确认失败原因来自“当前不支持回滚纠偏”

### Wave 3: Runtime Data Model

- 扩展 `PlayerRuntime`
- 增加窗口内接收序号、片段历史、连续确认序号、纠偏标记

### Wave 4: Input Intake And Processing

- 调整 `HandlePlayerInput`
- 调整 tick 内输入消费与确认序号推进规则
- 记录已处理输入片段历史

### Wave 5: Reconciliation

- 实现单玩家近窗口回放
- 偏差超过阈值时修正当前位置/朝向
- 触发现有状态同步链路

### Wave 6: Verification

- 运行新增测试
- 运行受影响旧测试
- 全部通过后写 cleanup receipt

## Ownership

- 主代理负责全部实现、验证与交付

## Verification Commands

```bash
env CCACHE_DISABLE=1 script/build_server.sh --debug
ctest --test-dir server/build-debug --output-on-failure -R "late_input|udp_sync_smoke|server_smoke|tick_dispatch_gate"
```

## Delivery Acceptance

- 只有在新增测试先失败后转绿，且受影响旧测试仍通过时，才允许声称 P2-1 服务器侧完成

## Rollback Rules

- 不改协议定义，除非测试或实现证明完全无法复用现有链路
- 若实现过程中发现需要房间级重放，立即停止并回到用户确认
