## Changes

- `server/docs/server_structure_audit.md`
  - 新增当前服务端结构审计文档。
  - 记录了目录结构、启动装配链路、配置/TCP/UDP/RoomManager/GameManager 的职责边界，以及本轮发现的文档漂移。

- `server/docs/AI_GUIDE.md`
  - 修正了 smoke test 的真实路径，改为 `server/tests/integration/*` 与 `server/tests/unit/*`。
  - 修正了 manager 私有拆分头的说明，明确真实位置是 `server/include/game/managers/internal/`。

- `README.md`
  - 修正项目结构示例，使其反映当前服务端目录、`game_config/` 和客户端高层布局。
  - 修正 `proto/message.proto` 文件名说明。
  - 修正服务端测试命令，改为 `script/build_server.sh --debug` 与 `ctest --test-dir server/build-debug --output-on-failure`。
  - 增加了 `server/docs/server_structure_audit.md` 的文档入口。

- `AI_GUIDE.md`
  - 回写了本轮执行摘要、验证结果与审计轨迹。

## Testing

- `script/build_server.sh --configure-only`
  - 通过。

- `script/build_server.sh --debug`
  - 通过。
  - 首次在沙箱内执行时因 `~/.cache/ccache` 临时目录只读失败，提权重跑后通过。

- `ctest --test-dir server/build-debug --output-on-failure`
  - 通过，3/3 测试成功。
  - 首次在沙箱内执行时，`server_smoke` 与 `udp_sync_smoke` 因本地 socket 创建受限失败；提权重跑后全部通过。
