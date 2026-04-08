## Changes

- `server/tests/integration/server_net_fix_smoke_test.cpp`
  - 新增服务器补充 smoke test。
  - 覆盖四类此前缺口：
    - `Heartbeat`
    - 升级完整链路：`UpgradeRequestAck`、`UpgradeOptionsAck`、`UpgradeRefreshRequest`、`UpgradeSelect`
    - `GameOver -> RoomManager::FinishGame -> 房间回非游戏态`
    - `server_metrics/...json` 落盘
  - 复用现有临时工作目录起服方式，并补了测试内自动清理 `server_metrics` 产物，避免污染仓库。
  - 执行过程中先让测试跑红，再把升级触发方式对齐到仓库里已验证的“带 `session_token` 的 UDP 输入”主路径，并修正了 `input_time.tick` 误用导致的输入过期判断问题，使测试稳定通过。

- `server/CMakeLists.txt`
  - 新增 `server_net_fix_smoke_test` 可执行目标。
  - 新增 `server_net_fix_smoke` 的 CTest 入口，并设置超时。

- `server/docs/net_fix/server_completion_checklist.md`
  - 新建服务器完成度清单。
  - 以“已实现且已验证 / 已实现但未验证 / 未实现”标记服务器各模块当前状态。
  - 把本次新增的验证入口与证据路径一并落文档，后续可直接作为服务器侧权威基线使用。

- 服务端主实现文件
  - 本次没有修改 `server/src/...` 下的服务器实现。
  - 原因是新增验证跑通后，当前缺口被确认主要在“清单缺失 + 自动化验证缺失”，而不是服务器主链路代码缺失。

## Testing

- 构建
  - 命令：`CCACHE_DIR=/tmp/ccache CCACHE_TEMPDIR=/tmp/ccache/tmp TMPDIR=/tmp/ccache/tmp script/build_server.sh --debug`
  - 结果：通过。
  - 说明：当前沙箱里 `~/.cache/ccache/tmp` 只读，因此构建时显式切到 `/tmp`，避免环境误报。

- 新增测试的 TDD 过程
  - 首次运行：`ctest --test-dir build-debug --output-on-failure -R server_net_fix_smoke`
  - 初始结果：失败，报 `等待消息超时: MSG_S2C_UPGRADE_REQUEST`。
  - 修正内容：
    - 把升级触发从测试里假设的 TCP 输入改为仓库现有已验证的 UDP 输入路径。
    - 去掉 `input_time.tick` 的错误填充，避免服务器把它当历史 tick 导致输入过期。
    - 增加初始 `GameStateSync` 校验与 TCP 队列 drain，消除完整测试集下的时序不稳定。
  - 修正后结果：通过。

- 服务器全量测试
  - 命令：`ctest --test-dir build-debug --output-on-failure`
  - 结果：4/4 通过。
  - 通过项：
    - `server_smoke`
    - `udp_sync_smoke`
    - `server_net_fix_smoke`
    - `config_loader_smoke`

- 测试产物检查
  - 检查 `server_metrics/`：测试结束后无残留文件。
  - 检查工作区：仅存在本次预期改动文件，无额外测试垃圾文件。
