# 2026-04-08 P2-1 Server Rollback Requirement

## Goal

完成服务器侧 `P2-1` 的最小闭环版本：对窗口内迟到输入进行历史重放校验，并在必要时通过现有状态同步链路回传权威纠偏。

## Deliverable

- 服务器代码改动
- 至少一个新增 failing-first 测试，覆盖迟到输入补洞后的确认序号与权威位置纠偏
- 更新后的构建与测试结果

## Constraints

- 只改服务器
- 不新增客户端协议依赖
- 不做房间级完整回滚，只处理单玩家移动/朝向与输入确认

## Acceptance Criteria

- 服务端不再因先收到高序号输入而错误推进 `last_processed_input_seq`
- 窗口内迟到输入补齐缺口后，服务端能基于历史重放修正当前玩家权威位置/朝向
- 修正结果通过现有状态同步链路可被观察到
- 超窗旧输入和重复旧输入仍被拒绝

## Manual Spot Checks

- `server/src/game/managers/game_manager_session.cpp`
- `server/src/game/managers/game_manager_tick_flow.cpp`
- `server/src/game/managers/game_manager_runtime.cpp`
- `server/src/game/managers/game_manager_sync.cpp`
- `server/include/game/managers/internal/game_manager_private_types.inc`
- `server/tests/integration/*`

## Non-Goals

- 敌人/射弹/掉落/伤害/升级的完整历史回滚
- 新增客户端消费逻辑
- 新增协议字段
