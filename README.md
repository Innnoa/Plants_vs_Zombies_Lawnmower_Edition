# 割草游戏 (LawnMower Game)

一款受《植物大战僵尸》和《咒语旅团》启发的多人生存 Roguelike 游戏。在无尽的僵尸浪潮中战斗，升级角色，解锁强大技能，体验紧张刺激的合作游戏。

[![License: MIT](https://img.shields.io/badge/License-MIT-yellow.svg)](https://opensource.org/licenses/MIT)
[![C++](https://img.shields.io/badge/C++-20-blue.svg)](https://isocpp.org/)
[![Java](https://img.shields.io/badge/Java-17-orange.svg)](https://www.oracle.com/java/)

## 项目简介

割草游戏是一款网络多人实时战斗游戏，具有成长系统和合作玩法。项目展示了权威服务器架构、使用 Protocol Buffers 的自定义网络协议以及流畅的游戏同步机制。

### 核心特性

- **多人支持**：最多支持 4 人合作模式
- **Roguelike 成长**：升级并从随机技能中选择强化
- **波次战斗**：面对不断增强的僵尸群
- **网络同步**：通过客户端预测和服务器校正实现流畅游戏体验
- **房间系统**：创建和加入游戏大厅
- **多种敌人类型**：各种具有不同行为和难度的僵尸类型

## 技术栈

### 服务端 (C++)
- **语言**：C++20
- **网络库**：Asio (TCP/UDP)
- **序列化**：Protocol Buffers 3.x
- **日志库**：spdlog
- **构建系统**：CMake 3.20+

### 客户端 (Java)
- **语言**：Java 17
- **游戏框架**：LibGDX
- **网络库**：Netty
- **序列化**：Protocol Buffers 3.x
- **构建系统**：Gradle

## 项目结构

```
LawnMower/
├── proto/                      # 共享的 Protocol Buffers 定义
│   └── messages.proto
│
├── server/                     # C++ 服务端
│   ├── include/
│   │   ├── network/           # TCP/UDP 服务器实现
│   │   ├── game/              # 游戏逻辑（世界、战斗、敌人）
│   │   └── utils/             # 工具类和辅助函数
│   ├── src/
│   ├── generated/             # 生成的 protobuf 代码
│   └── CMakeLists.txt
│
└── client/                     # Java 客户端
    ├── core/
    │   └── src/
    │       └── com/lawnmower/
    │           ├── network/   # 网络客户端
    │           ├── screens/   # 游戏界面（菜单、游戏等）
    │           ├── entities/  # 游戏实体（玩家、敌人、道具）
    │           └── systems/   # 游戏系统（渲染、输入）
    ├── assets/                # 游戏资源（纹理、音效）
    └── build.gradle
```

## 快速开始

### 环境要求

**服务端要求：**
- GCC 13+ 或 Clang 16+
- CMake 3.20+
- Protocol Buffers 3.20+
- Asio（仅头文件库）
- spdlog

**客户端要求：**
- JDK 17+
- Gradle 7.0+
- Protocol Buffers 3.20+

### 构建服务端

```bash
# 克隆仓库
git clone https://github.com/yourusername/lawnmower-game.git
cd lawnmower-game/server

# 安装依赖（Arch Linux）
sudo pacman -S cmake protobuf asio spdlog

# （可选）ccache 缓存目录建议放到 ~/.cache/ccache
# 这样不会在仓库根目录生成 .ccache/.ccache-tmp，也能避免某些环境默认临时目录不可写导致的编译报错。
# 在仓库根目录执行：
#   script/build_server.sh --debug
#   script/build_server.sh --release
#
# 或手动设置环境变量（等价）：
#   export CCACHE_DIR="$HOME/.cache/ccache"
#   export CCACHE_TEMPDIR="$CCACHE_DIR/tmp"
#   export TMPDIR="$CCACHE_TEMPDIR"
#
# 构建
mkdir build && cd build
cmake ..
make -j$(nproc)

# 运行
./server
```

### 构建客户端

```bash
cd ../client

# 构建
./gradlew desktop:dist

# 运行
./gradlew desktop:run
```

## 网络架构

### 协议设计

游戏使用 Protocol Buffers 进行消息序列化，并使用自定义数据包封装：

```protobuf
message Packet {
    uint32 msg_type = 1;    // 消息类型标识符
    bytes payload = 2;       // 序列化的消息内容
}
```

协议文件（单一来源）位于仓库根目录 `proto/message.proto`：
- 服务端：CMake 构建时自动通过 `protoc` 生成 `message.pb.*`（输出到 `server/build*/generated/`）。
- 客户端：当前仓库提交了生成后的 `client/core/src/main/java/lawnmower/Message.java`；当 proto 有变更时可用 `protoc --java_out=client/core/src/main/java -I proto proto/message.proto` 重新生成。

### 通信流程

1. **TCP**：用于可靠消息（登录、房间管理、游戏事件）
2. **UDP**：用于高频状态同步（玩家输入、游戏状态）

```
客户端                          服务器
  |                               |
  |--TCP: 登录请求---------------->|
  |<-TCP: 登录结果-----------------|
  |                               |
  |--UDP: 玩家输入---------------->|
  |<-UDP: 游戏状态-----------------|
  |                               |
  |--TCP: 创建房间---------------->|
  |<-TCP: 房间已创建---------------|
```

### 服务器更新频率

- **游戏循环**：60 Hz（每次 16.67ms）
- **状态广播**：20 Hz（50ms 间隔）
- **客户端输入频率**：客户端可发送的最快速度

## 游戏机制

### 玩家成长

- 击杀敌人获得经验值
- 升级解锁技能选择
- 每次升级从 3 个随机技能中选择
- 技能可叠加形成强力组合

### 敌人系统

| 敌人类型 | 生命值 | 速度 | 伤害 | 奖励 |
|---------|--------|------|------|------|
| 普通僵尸 | 30 | 60 | 5 | 10 经验 |
| 路障僵尸 | 60 | 50 | 8 | 20 经验 |
| 铁桶僵尸 | 120 | 40 | 10 | 40 经验 |
| 橄榄球僵尸 | 80 | 100 | 15 | 50 经验 |

### 技能系统

可用技能包括：
- 移动速度提升
- 攻击伤害加成
- 生命值上限增加
- 攻击速度提升
- 攻击范围扩大
- 生命偷取
- 范围伤害
- 更多技能...

## 开发指南

### 添加新消息类型

1. 在 `proto/messages.proto` 中定义消息
2. 分配唯一的消息类型 ID
3. 为服务端和客户端重新生成代码
4. 在两端实现处理程序

示例：

```protobuf
// 在 messages.proto 中
message C2S_UseSkill {
    uint32 skill_id = 1;
}

// 添加到消息类型枚举
// MSG_USE_SKILL = 250
```

### 实现新敌人

1. 在 `enemy_types.hpp` 中添加敌人配置
2. 在客户端创建对应的纹理/动画
3. 在 `EnemyManager` 中实现 AI 行为
4. 根据难度曲线更新生成逻辑

### 测试网络通信

使用 `tools/` 目录中提供的测试工具：

```bash
# 服务端数据包嗅探器
./tools/packet_sniffer --port 7777

# 用于负载测试的客户端模拟器
./tools/client_simulator --count 10
```

## 配置

### 服务器配置

编辑 `server_config.json`：

```json
{
    "tcp_port": 7777,
    "udp_port": 7778,
    "max_players_per_room": 4,
    "tick_rate": 60,
    "state_sync_rate": 20,
    "map_width": 2000,
    "map_height": 2000
}
```

### 客户端配置

编辑 `client_config.json`：

```json
{
    "server_host": "127.0.0.1",
    "server_tcp_port": 7777,
    "server_udp_port": 7778,
    "username": "Player",
    "music_volume": 0.7,
    "sfx_volume": 0.8
}
```

## 性能优化

### 服务端优化

- 碰撞检测使用空间分区
- 实体对象池
- 状态更新的差量压缩
- 使用 Asio 的多线程 I/O

### 客户端优化

- 本地玩家的客户端预测
- 实体插值实现平滑移动
- 渲染的精灵批处理
- 资源预加载和缓存

## 已知问题

- 高延迟（>200ms）可能导致输入延迟
- 大量实体（>500）会影响服务器性能
- 不可靠网络上的 UDP 丢包需要更好的处理

完整列表和解决方法请查看 [Issues](https://github.com/yourusername/lawnmower-game/issues)。

## 开发路线图

### 版本 1.0
- [ ] 基础多人功能
- [ ] 房间系统
- [ ] 核心战斗机制
- [ ] 等级成长
- [ ] 平衡性调整
- [ ] 性能优化

### 版本 2.0（计划中）
- [ ] 更多敌人类型
- [ ] Boss 战
- [ ] 强化道具
- [ ] 玩家自定义
- [ ] 持久化统计
- [ ] 排行榜

## 贡献指南

欢迎贡献！请阅读我们的 [贡献指南](CONTRIBUTING.md) 了解行为准则和提交 Pull Request 的流程。

### 开发工作流

1. Fork 本仓库
2. 创建特性分支 (`git checkout -b feature/amazing-feature`)
3. 提交更改 (`git commit -m '添加某个很棒的特性'`)
4. 推送到分支 (`git push origin feature/amazing-feature`)
5. 提交 Pull Request

## 测试

### 运行测试

```bash
# 服务端测试
cd server/build
ctest --verbose

# 客户端测试
cd client
./gradlew test
```

### 集成测试

详细的测试环境设置信息请查看 [测试指南](docs/TESTING.md)。

## 文档

更多文档可在 `docs/` 目录中找到：

- [架构概览](docs/ARCHITECTURE.md)
- [网络协议规范](docs/PROTOCOL.md)
- [游戏设计文档](docs/DESIGN.md)
- [API 参考](docs/API.md)

## 许可证

本项目采用 MIT 许可证 - 详见 [LICENSE](LICENSE) 文件。

## 致谢

- 灵感来源于《植物大战僵尸》（PopCap Games）
- 游戏机制受《吸血鬼幸存者》启发
- 网络架构基于 [Gaffer on Games](https://gafferongames.com/)
- 资源来源详见 [CREDITS.md](CREDITS.md)

## 作者

- **C++ 服务端开发者** - 服务器架构和游戏逻辑
- **Java 客户端开发者** - 客户端实现和 UI/UX

## 联系方式

项目链接：[https://github.com/yourusername/lawnmower-game](https://github.com/yourusername/lawnmower-game)

如有问题或需要支持，请在 GitHub 上提交 Issue。

---

**注意**：本项目使用《植物大战僵尸》的素材仅供教育目的。如需商业使用，请替换为原创素材。
