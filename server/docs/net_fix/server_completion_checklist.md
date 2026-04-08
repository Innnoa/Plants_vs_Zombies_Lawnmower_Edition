# 服务器完成度检查清单

更新日期：2026-04-08  
检查范围：仅服务器，不含客户端对齐

## 状态定义

- `已实现且已验证`：代码已落地，且有可运行的自动化验证
- `已实现但未验证`：代码已落地，但当前缺少对应自动化验证
- `未实现`：当前代码中未找到对应能力

## 当前结论

当前服务器的基础联机主链路已经完成，剩余问题主要不是“功能没写”，而是“清单与测试覆盖之前不完整”。本次补齐后，服务器侧已经有针对启动/配置、TCP 主流程、UDP 输入同步、心跳、升级流程、GameOver 回房间态、性能指标落盘的自动化验证。

## 检查项

| 模块 | 状态 | 代码依据 | 验证依据 | 备注 |
| --- | --- | --- | --- | --- |
| 启动与配置加载 | 已实现且已验证 | `server/src/main.cpp`、`server/src/config/*.cpp` | `config_loader_smoke_test.cpp` | 覆盖默认值回退、非法 JSON、边界 clamp |
| TCP 登录 | 已实现且已验证 | `server/src/network/tcp/session_auth.cpp` | `server_smoke_test.cpp` | 覆盖登录成功与未登录保护 |
| TCP 心跳 | 已实现且已验证 | `server/src/network/tcp/session_auth.cpp` | `server_net_fix_smoke_test.cpp` | 验证 `timestamp` 与 `online_players` |
| TCP 重连 | 已实现且已验证 | `server/src/network/tcp/session_auth.cpp` | `server_smoke_test.cpp` | 覆盖错误 token、有效重连、超宽限期失败 |
| 房间流（建房/列表/加房/准备/开局） | 已实现且已验证 | `server/src/network/tcp/session_room.cpp`、`room_manager_*.cpp` | `server_smoke_test.cpp` | 覆盖房主权限、未准备拦截、开局成功 |
| UDP 输入注册与状态同步 | 已实现且已验证 | `server/src/network/udp/udp_server.cpp`、`game_manager_sync*.cpp` | `udp_sync_smoke_test.cpp` | 覆盖 `session_token` 校验、delta 收敛 |
| 升级请求/选项/刷新/选择 | 已实现且已验证 | `server/src/network/tcp/session_gameplay.cpp`、`game_manager_upgrade.cpp` | `server_net_fix_smoke_test.cpp` | 覆盖 `UpgradeRequestAck`、`UpgradeOptionsAck`、`UpgradeRefreshRequest`、`UpgradeSelect` |
| GameOver 广播 | 已实现且已验证 | `game_manager_combat_gameover.cpp`、`game_manager_event_dispatch.cpp` | `server_net_fix_smoke_test.cpp` | 覆盖单人死亡后的 `S2C_GameOver` |
| GameOver 后房间回非游戏态 | 已实现且已验证 | `room_manager_gameplay.cpp` | `server_net_fix_smoke_test.cpp` | 验证 `FinishGame` 后房间列表 `is_playing=false` |
| 性能指标落盘 | 已实现且已验证 | `game_manager_metrics.cpp` | `server_net_fix_smoke_test.cpp` | 验证生成新的 `server_metrics/...json` 文件且内容有效 |
| 文档一致性维护 | 已实现但未验证 | `server/docs/` | 本清单 + `server_structure_audit.md` | 需要后续改动时持续同步更新 |

## 本次新增验证入口

- `server/tests/integration/server_net_fix_smoke_test.cpp`
  - 心跳
  - 升级完整链路
  - GameOver 后房间状态恢复
  - `server_metrics` 落盘

## 建议

1. 后续服务端协议变更时，优先更新本清单，再改客户端。
2. 若新增关键游戏流程，优先先补对应 integration smoke，再补文档说明。
3. `server/docs/net_fix/` 建议继续作为服务器联机修复与完成度跟踪的权威目录，不再只依赖聊天记录。
