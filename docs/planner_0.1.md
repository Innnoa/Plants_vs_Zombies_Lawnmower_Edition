# 植物大战僵尸割草游戏 - 详细开发计划

## 📋 项目信息

- **开发周期**: 预计 8-10 周
- **人员**: 2人（C++ 服务端 + Java 客户端）
- **每天投入**: 建议 2-4 小时
- **技术栈**: C++20, Java 17, LibGDX, Asio, Protobuf

------

## 🏁 Phase 0: 环境搭建与协议定义（Day 1-3）

### Day 1: 环境安装

#### 🐧 你（C++ - Arch Linux）

**时间**: 1-2小时

- [ ] 安装基础工具

```bash
sudo pacman -S base-devel cmake git ninja
sudo pacman -S protobuf asio spdlog glm
```

- [ ] 验证安装

```bash
protoc --version  # 应该 >= 3.20
cmake --version   # 应该 >= 3.20
g++ --version     # 应该 >= 13
```

- [ ] 创建项目结构

```bash
mkdir -p ~/projects/LawnMowerServer/{proto,include,src,generated}
cd ~/projects/LawnMowerServer
git init
```

- [ ] 创建 .gitignore

```gitignore
build/
generated/*.pb.*
*.o
*.exe
*.out
.vscode/
.idea/
```

#### 🪟 队友（Java - Windows）

**时间**: 1-2小时

- [ ] 安装 JDK 17
  - 下载: https://adoptium.net/
  - 验证: `java -version`
- [ ] 安装 Protobuf 编译器
  - 下载: https://github.com/protocolbuffers/protobuf/releases
  - 解压到 `C:\Tools\protoc`
  - 添加 `C:\Tools\protoc\bin` 到 PATH
  - 验证: `protoc --version`
- [ ] 安装 IntelliJ IDEA Community（可选但推荐）
- [ ] 创建 LibGDX 项目
  - 下载 gdx-liftoff: https://github.com/tommyettinger/gdx-liftoff/releases
  - 运行创建项目:
    - Name: LawnMowerClient
    - Package: com.lawnmower
    - 勾选: Desktop platform

#### 🤝 一起完成

**时间**: 30分钟-1小时（晚上视频通话）

- [ ] 创建 GitHub 仓库（决定谁创建）
- [ ] 添加双方为协作者
- [ ] 确认各自环境搭建成功
- [ ] 约定每日同步时间（建议：每晚9点，5-10分钟）

------

### Day 2: Protobuf 协议定义

#### 🤝 上午：一起讨论协议（1小时视频/语音）

**讨论并确定**:

- [ ] 消息类型编号规则
- [ ] 游戏常量（地图大小、移动速度等）
- [ ] 网络端口分配

#### 🐧 你（C++）

**时间**: 2-3小时

- [ ] 创建 `proto/messages.proto` 文件

```protobuf
syntax = "proto3";
package lawnmower;

// 基础类型
message Vector2 {
    float x = 1;
    float y = 2;
}

// 心跳
message C2S_Heartbeat {
    uint64 timestamp = 1;
}

message S2C_Heartbeat {
    uint64 timestamp = 1;
}

// 登录
message C2S_Login {
    string player_name = 1;
}

message S2C_LoginResult {
    bool success = 1;
    uint32 player_id = 2;
    string message = 3;
}

// 输入
message C2S_PlayerInput {
    uint32 sequence = 1;
    Vector2 move_direction = 2;
    bool is_attacking = 3;
}

// 游戏状态
message PlayerState {
    uint32 player_id = 1;
    Vector2 position = 2;
    float rotation = 3;
    int32 health = 4;
    int32 max_health = 5;
}

message S2C_GameState {
    uint32 tick = 1;
    repeated PlayerState players = 2;
}

// 消息封装
message Packet {
    uint32 msg_type = 1;
    bytes payload = 2;
}
```

- [ ] 推送到 GitHub

```bash
git add proto/messages.proto
git commit -m "添加初始 Protobuf 协议定义"
git push
```

- [ ] 通知队友拉取代码

#### 🪟 队友（Java）

**时间**: 2-3小时

- [ ] 从 GitHub 拉取 proto 文件

```bash
git pull
```

- [ ] 修改 `build.gradle` 添加依赖

```groovy
project(":core") {
    dependencies {
        // ... 已有依赖
        
        // 网络库
        implementation 'io.netty:netty-all:4.1.100.Final'
        
        // Protobuf
        implementation 'com.google.protobuf:protobuf-java:3.25.1'
    }
}
```

- [ ] 同步 Gradle: `./gradlew build`
- [ ] 创建 `core/src/com/lawnmower/network` 包

------

### Day 3: Protobuf 代码生成与测试

#### 🐧 你（C++）

**时间**: 2-3小时

- [ ] 创建 `CMakeLists.txt`

```cmake
cmake_minimum_required(VERSION 3.20)
project(LawnMowerServer CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# 查找依赖
find_package(Protobuf REQUIRED)
find_package(spdlog REQUIRED)

# Protobuf 代码生成
set(PROTO_DIR ${CMAKE_SOURCE_DIR}/proto)
set(GEN_DIR ${CMAKE_SOURCE_DIR}/generated)
file(MAKE_DIRECTORY ${GEN_DIR})

file(GLOB PROTO_FILES "${PROTO_DIR}/*.proto")
set(GENERATED_SRCS)

foreach(PROTO ${PROTO_FILES})
    get_filename_component(NAME ${PROTO} NAME_WE)
    add_custom_command(
        OUTPUT ${GEN_DIR}/${NAME}.pb.cc ${GEN_DIR}/${NAME}.pb.h
        COMMAND protobuf::protoc
        ARGS --cpp_out=${GEN_DIR} --proto_path=${PROTO_DIR} ${PROTO}
        DEPENDS ${PROTO}
    )
    list(APPEND GENERATED_SRCS ${GEN_DIR}/${NAME}.pb.cc)
endforeach()

# Proto 库
add_library(proto_lib STATIC ${GENERATED_SRCS})
target_include_directories(proto_lib PUBLIC ${GEN_DIR})
target_link_libraries(proto_lib PUBLIC protobuf::libprotobuf)

# 主程序
add_executable(server src/main.cpp)
target_include_directories(server PRIVATE include)
target_link_libraries(server PRIVATE proto_lib spdlog::spdlog)
```

- [ ] 创建测试代码 `src/main.cpp`

```cpp
#include <iostream>
#include <spdlog/spdlog.h>
#include "messages.pb.h"

int main() {
    GOOGLE_PROTOBUF_VERIFY_VERSION;
    
    // 测试序列化
    lawnmower::C2S_Login login;
    login.set_player_name("测试玩家");
    
    std::string data = login.SerializeAsString();
    spdlog::info("序列化大小: {} 字节", data.size());
    
    // 测试反序列化
    lawnmower::C2S_Login parsed;
    parsed.ParseFromString(data);
    spdlog::info("玩家名: {}", parsed.player_name());
    
    // 测试 Packet 封装
    lawnmower::Packet packet;
    packet.set_msg_type(1);
    packet.set_payload(data);
    
    spdlog::info("Packet 大小: {} 字节", 
                 packet.SerializeAsString().size());
    
    google::protobuf::ShutdownProtobufLibrary();
    return 0;
}
```

- [ ] 编译运行

```bash
mkdir build && cd build
cmake ..
make
./server
```

- [ ] 验证输出正确，截图发给队友

#### 🪟 队友（Java）

**时间**: 2-3小时

- [ ] 复制 proto 文件到项目

```cmd
# 在项目根目录
mkdir proto
copy ..\LawnMowerServer\proto\messages.proto proto\
```

- [ ] 生成 Java 代码

```cmd
protoc --java_out=core/src proto/messages.proto
```

- [ ] 创建测试类 `core/src/com/lawnmower/ProtobufTest.java`

```java
package com.lawnmower;

import com.lawnmower.Lawnmower.*;

public class ProtobufTest {
    public static void main(String[] args) throws Exception {
        // 测试序列化
        C2SLogin login = C2SLogin.newBuilder()
            .setPlayerName("测试玩家")
            .build();
        
        byte[] data = login.toByteArray();
        System.out.println("序列化大小: " + data.length + " 字节");
        
        // 测试反序列化
        C2SLogin parsed = C2SLogin.parseFrom(data);
        System.out.println("玩家名: " + parsed.getPlayerName());
        
        // 测试 Packet 封装
        Packet packet = Packet.newBuilder()
            .setMsgType(1)
            .setPayload(com.google.protobuf.ByteString.copyFrom(data))
            .build();
        
        System.out.println("Packet 大小: " + packet.toByteArray().length);
    }
}
```

- [ ] 运行测试

```cmd
cd core/src
javac -cp "..\..\lib\*" com/lawnmower/ProtobufTest.java
java -cp ".;..\..\lib\*" com.lawnmower.ProtobufTest
```

- [ ] 验证输出，截图发给你

#### 🤝 晚上：Day 3 检查点（20分钟）

- [ ] 对比双方输出，序列化大小应该相同
- [ ] 确认协议无误
- [ ] 讨论 Phase 1 任务分配

------

## 🔗 Phase 1: 基础网络通信（Day 4-10）

### Day 4-5: TCP 回声服务器/客户端

#### 🐧 你（C++ - Day 4）

**时间**: 3-4小时

**目标**: 实现一个简单的 TCP Echo 服务器

- [ ] 创建 `include/network/tcp_server.hpp`

```cpp
#pragma once
#include <asio.hpp>
#include <memory>
#include <functional>

using asio::ip::tcp;

class TcpSession : public std::enable_shared_from_this<TcpSession> {
public:
    TcpSession(tcp::socket socket);
    void start();
    void send(const std::string& data);
    
private:
    void do_read();
    void do_write();
    
    tcp::socket socket_;
    std::array<char, 1024> buffer_;
    std::string write_data_;
};

class TcpServer {
public:
    TcpServer(asio::io_context& io, uint16_t port);
    void start();
    
private:
    void do_accept();
    
    asio::io_context& io_context_;
    tcp::acceptor acceptor_;
};
```

- [ ] 实现 `src/network/tcp_server.cpp`（回声功能）
- [ ] 修改 `src/main.cpp` 启动服务器

```cpp
#include <spdlog/spdlog.h>
#include "network/tcp_server.hpp"

int main() {
    try {
        asio::io_context io;
        TcpServer server(io, 7777);
        
        spdlog::info("服务器启动，监听端口 7777");
        server.start();
        
        io.run();
    } catch (std::exception& e) {
        spdlog::error("错误: {}", e.what());
    }
    return 0;
}
```

- [ ] 编译测试

```bash
cd build
make
./server
```

- [ ] 用 telnet 自测

```bash
# 另开一个终端
telnet localhost 7777
# 输入任何内容，应该原样返回
```

#### 🪟 队友（Java - Day 5）

**时间**: 3-4小时

**目标**: 实现简单的 TCP 客户端

- [ ] 创建 `core/src/com/lawnmower/network/TcpClient.java`

```java
package com.lawnmower.network;

import java.io.*;
import java.net.*;

public class TcpClient {
    private Socket socket;
    private PrintWriter out;
    private BufferedReader in;
    
    public void connect(String host, int port) throws IOException {
        socket = new Socket(host, port);
        out = new PrintWriter(socket.getOutputStream(), true);
        in = new BufferedReader(
            new InputStreamReader(socket.getInputStream()));
        System.out.println("已连接到 " + host + ":" + port);
    }
    
    public void send(String message) {
        out.println(message);
    }
    
    public String receive() throws IOException {
        return in.readLine();
    }
    
    public void close() throws IOException {
        socket.close();
    }
    
    public static void main(String[] args) {
        TcpClient client = new TcpClient();
        try {
            client.connect("127.0.0.1", 7777);
            
            client.send("Hello Server!");
            String response = client.receive();
            System.out.println("服务器响应: " + response);
            
            client.close();
        } catch (IOException e) {
            e.printStackTrace();
        }
    }
}
```

- [ ] 编译运行

```cmd
cd core/src
javac com/lawnmower/network/TcpClient.java
java com.lawnmower.network.TcpClient
```

- [ ] 验证能收到回声

#### 🤝 Day 5 晚上：第一次联调（30分钟）

**前提**: 确认你们在同一 WiFi

- [ ] 你查看 IP: `ip addr | grep "inet 192"`
- [ ] 你运行服务器
- [ ] 队友修改代码连接你的 IP
- [ ] 队友运行客户端
- [ ] **验证**: 队友能收到服务器回声

**如果连不上**:

- 检查防火墙: `sudo ufw allow 7777`
- 队友 ping 你的 IP

------

### Day 6-7: 集成 Protobuf 消息

#### 🐧 你（C++ - Day 6）

**时间**: 3-4小时

**目标**: 服务器支持接收/发送 Protobuf 消息

- [ ] 修改 TcpSession，添加消息处理

```cpp
// tcp_server.hpp 中添加
void handle_packet(const lawnmower::Packet& packet);
void send_packet(const lawnmower::Packet& packet);
```

- [ ] 实现消息分发逻辑

```cpp
void TcpSession::handle_packet(const lawnmower::Packet& packet) {
    switch (packet.msg_type()) {
    case 1: { // Login
        lawnmower::C2S_Login login;
        login.ParseFromString(packet.payload());
        
        spdlog::info("玩家登录: {}", login.player_name());
        
        // 回复
        lawnmower::S2C_LoginResult result;
        result.set_success(true);
        result.set_player_id(1001);
        result.set_message("登录成功");
        
        lawnmower::Packet reply;
        reply.set_msg_type(2);
        reply.set_payload(result.SerializeAsString());
        
        send_packet(reply);
        break;
    }
    default:
        spdlog::warn("未知消息类型: {}", packet.msg_type());
    }
}
```

- [ ] 添加消息长度前缀（防止粘包）

```cpp
// 发送: [4字节长度][消息内容]
void send_packet(const Packet& packet) {
    std::string data = packet.SerializeAsString();
    uint32_t len = data.size();
    // 写入长度
    // 写入数据
}
```

- [ ] 测试：启动服务器，等待队友测试

#### 🪟 队友（Java - Day 7）

**时间**: 3-4小时

**目标**: 客户端发送/接收 Protobuf 消息

- [ ] 修改 TcpClient，添加 Protobuf 支持

```java
public void sendPacket(Packet packet) throws IOException {
    byte[] data = packet.toByteArray();
    
    // 写长度（4字节）
    DataOutputStream dos = new DataOutputStream(
        socket.getOutputStream());
    dos.writeInt(data.length);
    dos.write(data);
    dos.flush();
}

public Packet receivePacket() throws IOException {
    DataInputStream dis = new DataInputStream(
        socket.getInputStream());
    
    // 读长度
    int len = dis.readInt();
    
    // 读数据
    byte[] data = new byte[len];
    dis.readFully(data);
    
    return Packet.parseFrom(data);
}
```

- [ ] 测试登录流程

```java
public static void main(String[] args) {
    TcpClient client = new TcpClient();
    try {
        client.connect("你的IP", 7777);
        
        // 发送登录
        C2SLogin login = C2SLogin.newBuilder()
            .setPlayerName("玩家1")
            .build();
        
        Packet packet = Packet.newBuilder()
            .setMsgType(1)
            .setPayload(login.toByteString())
            .build();
        
        client.sendPacket(packet);
        
        // 接收响应
        Packet response = client.receivePacket();
        S2CLoginResult result = S2CLoginResult.parseFrom(
            response.getPayload());
        
        System.out.println("登录结果: " + result.getMessage());
        System.out.println("玩家ID: " + result.getPlayerId());
        
        client.close();
    } catch (Exception e) {
        e.printStackTrace();
    }
}
```

#### 🤝 Day 7 晚上：Protobuf 联调（30分钟）

- [ ] 你启动服务器

- [ ] 队友运行客户端

- [ ] 

  验证

  :

  - 客户端能发送登录消息
  - 服务器能解析并回复
  - 客户端能收到登录成功响应

- [ ] 截图保存测试结果

------

### Day 8-10: 房间系统

#### 🐧 你（C++ - Day 8-9）

**时间**: 每天3小时

- [ ] **Day 8**: 实现房间管理器

```cpp
// include/game/room_manager.hpp
class GameRoom {
public:
    uint32_t room_id;
    std::string name;
    std::vector<uint32_t> player_ids;
    uint32_t host_id;
    bool is_playing = false;
    
    bool add_player(uint32_t player_id);
    void remove_player(uint32_t player_id);
    void start_game();
};

class RoomManager {
public:
    GameRoom* create_room(const std::string& name, uint32_t host);
    GameRoom* find_room(uint32_t room_id);
    void remove_room(uint32_t room_id);
    std::vector<RoomInfo> get_room_list();
    
private:
    std::unordered_map<uint32_t, GameRoom> rooms_;
    uint32_t next_room_id_ = 1;
};
```

- [ ] **Day 9**: 添加房间相关的消息处理
  - 创建房间
  - 加入房间
  - 离开房间
  - 广播房间更新
- [ ] 测试：多个 telnet 连接模拟多玩家

#### 🪟 队友（Java - Day 8-10）

**时间**: 每天3小时

- [ ] **Day 8**: 设计主菜单 UI

```java
public class MainMenuScreen implements Screen {
    private Stage stage;
    private TextField nameField;
    private TextButton connectButton;
    
    @Override
    public void show() {
        stage = new Stage();
        
        // 名字输入框
        nameField = new TextField("", skin);
        nameField.setMessageText("输入玩家名");
        
        // 连接按钮
        connectButton = new TextButton("连接服务器", skin);
        connectButton.addListener(new ClickListener() {
            @Override
            public void clicked(InputEvent event, float x, float y) {
                connectToServer();
            }
        });
        
        // 布局...
    }
    
    private void connectToServer() {
        String name = nameField.getText();
        // 连接到服务器，发送登录消息
        game.setScreen(new RoomListScreen(game));
    }
}
```

- [ ] **Day 9**: 实现房间列表界面
  - 显示所有房间
  - 创建房间按钮
  - 加入房间按钮
- [ ] **Day 10**: 实现房间等待界面
  - 显示房间内玩家列表
  - 准备按钮
  - 开始游戏按钮（房主）

#### 🤝 Day 10 晚上：房间系统联调（1小时）

- [ ] 测试完整流程：
  1. 两个客户端同时连接
  2. 一个创建房间
  3. 另一个加入房间
  4. 双方能看到对方
  5. 房主点击开始

------

## ⚔️ Phase 2: 核心游戏逻辑（Day 11-25）

### Day 11-13: 游戏场景基础

#### 🐧 你（C++ - Day 11-12）

**Day 11**:

- [ ] 创建游戏世界类

```cpp
class GameWorld {
public:
    void update(float delta_time);
    void add_player(uint32_t player_id);
    void remove_player(uint32_t player_id);
    
    PlayerState get_player_state(uint32_t id);
    std::vector<PlayerState> get_all_states();
    
private:
    std::unordered_map<uint32_t, Player> players_;
};
```

**Day 12**:

- [ ] 实现游戏主循环（60 FPS）

```cpp
class GameLoop {
    void run() {
        using namespace std::chrono;
        auto last_time = high_resolution_clock::now();
        const float dt = 1.0f / 60.0f;
        
        while (running_) {
            auto current = high_resolution_clock::now();
            float elapsed = duration<float>(current - last_time).count();
            
            if (elapsed >= dt) {
                update(dt);
                last_time = current;
            }
        }
    }
};
```

- [ ] 实现玩家移动逻辑

#### 🪟 队友（Java - Day 11-13）

**Day 11**:

- [ ] 创建游戏场景框架

```java
public class GameScreen implements Screen {
    private OrthographicCamera camera;
    private SpriteBatch batch;
    private Texture playerTexture;
    
    @Override
    public void show() {
        camera = new OrthographicCamera();
        camera.setToOrtho(false, 800, 600);
        batch = new SpriteBatch();
        
        // 加载植物素材
        playerTexture = new Texture("player.png");
    }
    
    @Override
    public void render(float delta) {
        // 清屏
        Gdx.gl.glClear(GL20.GL_COLOR_BUFFER_BIT);
        
        // 渲染
        camera.update();
        batch.setProjectionMatrix(camera.combined);
        batch.begin();
        // 绘制玩家...
        batch.end();
    }
}
```

**Day 12**:

- [ ] 添加输入处理

```java
private Vector2 getMovementInput() {
    Vector2 input = new Vector2();
    if (Gdx.input.isKeyPressed(Input.Keys.W)) input.y += 1;
    if (Gdx.input.isKeyPressed(Input.Keys.S)) input.y -= 1;
    if (Gdx.input.isKeyPressed(Input.Keys.A)) input.x -= 1;
    if (Gdx.input.isKeyPressed(Input.Keys.D)) input.x += 1;
    return input.nor(); // 归一化
}
```

**Day 13**:

- [ ] 实现发送输入到服务器
- [ ] 实现客户端预测（本地立即移动）

#### 🤝 Day 13 晚上：移动同步测试

- [ ] 单人移动流畅度测试
- [ ] 双人互相能看到对方移动

------

### Day 14-18: 敌人系统

#### 🐧 你（C++ - Day 14-16）

**Day 14**:

- [ ] 定义敌人类型配置

```cpp
struct EnemyType {
    uint32_t type_id;
    std::string name;
    int32_t health;
    float speed;
    int32_t damage;
    int32_t exp_reward;
};

// PvZ 僵尸对应
const EnemyType ENEMY_TYPES[] = {
    {1, "普通僵尸", 30, 60.0f, 5, 10},
    {2, "路障僵尸", 60, 50.0f, 8, 20},
    {3, "铁桶僵尸", 120, 40.0f, 10, 40},
};
```

**Day 15**:

- [ ] 实现敌人生成器

```cpp
class EnemySpawner {
    void update(float delta);
    void spawn_wave(int wave_number);
};
```

**Day 16**:

- [ ] 实现敌人 AI（寻找最近玩家）
- [ ] 添加敌人到状态同步消息中

#### 🪟 队友（Java - Day 14-18）

**Day 14-15**:

- [ ] 准备僵尸素材
  - 从 PvZ 提取/网上找素材
  - 整理成精灵表

**Day 16**:

- [ ] 创建敌人渲染类

```java
public class Enemy {
    private int enemyId;
    private int type;
    private Vector2 position;
    private Texture texture;
    private Animation<TextureRegion> walkAnimation;
    
    public void render(SpriteBatch batch) {
        // 渲染僵尸动画
    }
}
```

**Day 17**:

- [ ] 实现敌人管理器（根据服务器状态更新）
- [ ] 添加插值（让敌人移动平滑）

**Day 18**:

- [ ] 优化渲染性能（批量绘制）

#### 🤝 Day 18 晚上：敌人系统测试

- [ ] 验证敌人生成
- [ ] 验证敌人会追踪玩家
- [ ] 验证多个敌人时性能

------

### Day 19-22: 战斗系统

#### 🐧 你（C++ - Day 19-21）

**Day 19**:

- [ ] 实现碰撞检测系统

```cpp
class CollisionSystem {
    bool check_circle_collision(Vector2 pos1, float r1, 
                                Vector2 pos2, float r2);
    
    std::vector<Enemy*> get_enemies_in_range(
        Vector2 center, float radius);
};
```

**Day 20**:

- [ ] 实现战斗系统

```cpp
class CombatSystem {
    void update(float delta);
    void handle_player_attack(uint32_t player_id);
    void handle_enemy_attack(uint32_t enemy_id, uint32_t player_id);
};
```

**Day 21**:

- [ ] 添加战斗事件消息
  - 玩家受伤
  - 敌人死亡
  - 获得经验

#### 🪟 队友（Java - Day 19-22）

**Day 19**:

- [ ] 添加攻击输入（鼠标点击 / 空格键）
- [ ] 发送攻击指令到服务器

**Day 20**:

- [ ] 实现攻击动画

```java
private void playAttackAnimation() {
    // 豌豆射手发射动画
}
```

**Day 21**:

- [ ] 添加伤害数字显示

```java
class DamageNumber {
    Vector2 position;
    int damage;
    float lifetime;
    
    void update(float delta) {
        position.y += 50 * delta; // 向上飘
        lifetime -= delta;
    }
}
```

**Day 22**:

- [ ] 添加血条显示（玩家和敌人）
- [ ] 添加音效（攻击、受伤、死亡）

#### 🤝 Day 22 晚上：战斗系统测试

- [ ] 验证攻击能造成伤害
- [ ] 验证敌人会反击
- [ ] 验证死亡逻辑正确

------

### Day 23-25: 升级系统

#### 🐧 你（C++ - Day 23-24）

**Day 23**:

- [ ] 实现经验系统

```cpp
class GrowthSystem {
    void add_exp(uint32_t player_id, int32_t exp);
    bool check_level_up(uint32_t player_id);
    std::vector<SkillOption> generate_skill_options();
};
```

**Day 24**:

- [ ] 定义技能配置

```cpp
enum SkillId {
    SKILL_SPEED_UP = 1,
    SKILL_ATTACK_UP = 2,
    SKILL_HEALTH_UP = 3,
    // ...
};

struct Skill {
    SkillId id;
    std::string name;
    std::string desc;
    std::function<void(Player&)> apply;
};
```

#### 🪟 队友（Java - Day 23-25）

**Day 23**:

- [ ] 添加 HUD（显示等级、经验、血量）

```java
public class HUD {
    private BitmapFont font;
    
    public void render(SpriteBatch batch, PlayerState player) {
        font.draw(batch, "Lv." + player.getLevel(), 10, 590);
        // 绘制经验条
        // 绘制血条
    }
}
```

**Day 24**:

- [ ] 实现升级选择界面

```java
public class SkillSelectDialog extends Dialog {
    public SkillSelectDialog(List<SkillOption> options) {
        // 显示 3 个技能选项
        // 点击选择后发送到服务器
    }
}
```

**Day 25**:

- [ ] 优化 UI 交互

#### 🤝 Day 25 晚上：第一次完整游戏测试

- [ ] 完整流程测试：登录 → 创建房间 → 开始游戏 → 击杀敌人 → 升级 → 游戏结束
- [ ] 记录问题清单

------

## 🎨 Phase 3: 完善与优化（Day 26-40）

### Day 26-30: 道具与特效

#### 🐧 你（C++ - Day 26-28）

- [ ] Day 26: 实现掉落系统
- [ ] Day 27: 实现道具拾取逻辑

#### 🪟 队友（Java - Day 26-30）

- [ ] Day 27-28: 实现粒子特效系统
  - [ ] Day 29: 添加音效和背景音乐

- [ ] Day 30: UI 美化

------

### Day 31-35: Bug修复与平衡性调整

#### 🤝 每天（2人一起）

- [ ] 游戏测试 1 小时
- [ ] 修复发现的 Bug
- [ ] 调整游戏数值（敌人强度、经验曲线等）
- [ ] 优化网络同步（减少延迟、丢包处理）

------

### Day 36-40: 最终优化与发布准备

#### 🐧 你（C++）

- [ ] 性能优化（内存、CPU）
- [ ] 添加服务器配置文件
- [ ] 编写服务器部署文档

#### 🪟 队友（Java）

- [ ] 打包发布版本
- [ ] 制作启动器
- [ ] 编写玩家手册

#### 🤝 一起

- [ ] 录制演示视频
- [ ] 撰写项目报告
- [ ] 准备答辩 PPT

------

## 📞 每日协作规范

### 每天固定时间（建议晚上 9 点）

**5 分钟快速同步**:

1. 我今天完成了: ___
2. 我明天计划: ___
3. 遇到的问题: ___

### 每周末（1-2 小时）

**深度集成与复盘**:

1. 合并代码，解决冲突
2. 端到端完整测试
3. 讨论下周计划
4. 更新协议（如需要）

### 紧急问题处理

- 卡住超过 1 小时 → 拍照/截图发给对方
- 无法解决 → 视频通话一起 debug
- 设计分歧 → 快速讨论，不拖延

------

## ✅ 关键里程碑

| 里程碑       | 日期   | 标志                         |
| ------------ | ------ | ---------------------------- |
| 环境搭建完成 | Day 3  | 双方能运行 Protobuf 测试代码 |
| 基础网络完成 | Day 10 | 双人能进入同一房间           |
| 核心玩法完成 | Day 25 | 能完整玩一局游戏             |
| 项目完成     | Day 40 | 可打包发布                   |

------

## 🎯 工作量预估

| 任务类型 | C++ 服务端 | Java 客户端 |
| -------- | ---------- | ----------- |
| 网络通信 | 35%        | 25%         |
| 游戏逻辑 | 45%        | 30%         |
| UI/渲染  | 5%         | 35%         |
| 优化调试 | 15%        | 10%         |

**总体**: 两人工作量基本平衡，但前期你（C++）会稍重，后期队友（Java）会稍重。

------

## 📝 建议

1. **严格按天推进**: 不要拖延，否则后面会很赶
2. **每日提交代码**: 养成好习惯，方便回滚
3. **提前准备素材**: Java 端尽早准备好所有图片、音效
4. **遇到问题不要硬扛**: 2 小时解决不了就找对方
5. **保持沟通**: 哪怕今天没写代码，也发个消息同步一下

祝你们项目顺利！💪