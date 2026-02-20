#include <cmath>
#include <filesystem>
#include <fstream>
#include <functional>
#include <iostream>
#include <stdexcept>
#include <string>
#include <unistd.h>
#include <vector>

#include "config/enemy_types_config.hpp"
#include "config/item_types_config.hpp"
#include "config/player_roles_config.hpp"
#include "config/server_config.hpp"
#include "config/upgrade_config.hpp"

namespace {
namespace fs = std::filesystem;

[[noreturn]] void Fail(const std::string& msg) {
  throw std::runtime_error(msg);
}

void Expect(bool cond, const std::string& msg) {
  if (!cond) {
    Fail(msg);
  }
}

void ExpectNear(float actual, float expected, float eps,
                const std::string& msg) {
  if (std::fabs(actual - expected) > eps) {
    Fail(msg + " actual=" + std::to_string(actual) +
         " expected=" + std::to_string(expected));
  }
}

class TempWorkspace final {
 public:
  TempWorkspace() {
    std::string pattern =
        (fs::temp_directory_path() / "config-loader-smoke-XXXXXX").string();
    std::vector<char> buffer(pattern.begin(), pattern.end());
    buffer.push_back('\0');
    char* created = ::mkdtemp(buffer.data());
    if (created == nullptr) {
      Fail("创建临时目录失败");
    }
    root_ = fs::path(created);
    std::error_code ec;
    fs::create_directories(root_ / "game_config", ec);
    if (ec) {
      Fail("创建 game_config 目录失败: " + ec.message());
    }
  }

  ~TempWorkspace() {
    std::error_code ec;
    fs::remove_all(root_, ec);
  }

  fs::path Root() const { return root_; }
  fs::path ConfigPath(std::string_view filename) const {
    return root_ / "game_config" / std::string(filename);
  }

 private:
  fs::path root_;
};

class ScopedCurrentPath final {
 public:
  explicit ScopedCurrentPath(const fs::path& target)
      : old_(fs::current_path()) {
    fs::current_path(target);
  }
  ~ScopedCurrentPath() {
    std::error_code ec;
    fs::current_path(old_, ec);
  }

 private:
  fs::path old_;
};

void WriteFile(const fs::path& path, const std::string& content) {
  std::ofstream out(path);
  if (!out.is_open()) {
    Fail("写入文件失败: " + path.string());
  }
  out << content;
}

void TestServerConfigTypeAndRangeGuards() {
  TempWorkspace ws;
  ScopedCurrentPath cwd(ws.Root());
  WriteFile(ws.ConfigPath("server_config.json"),
            R"json({
  "tcp_port": "bad",
  "udp_port": -1,
  "state_sync_rate": 29.5,
  "move_speed": 123.5,
  "reconnect_grace_seconds": 9999
})json");

  ServerConfig cfg;
  const bool loaded = LoadServerConfig(&cfg);
  Expect(loaded, "server_config 应该加载成功");
  Expect(cfg.tcp_port == 7777, "tcp_port 类型错误时应保留默认值");
  Expect(cfg.udp_port == 7778, "udp_port 负数时应保留默认值");
  Expect(cfg.state_sync_rate == 30, "state_sync_rate 非整数时应保留默认值");
  ExpectNear(cfg.move_speed, 123.5f, 1e-4f, "move_speed 应按配置生效");
  ExpectNear(cfg.reconnect_grace_seconds, 600.0f, 1e-4f,
             "reconnect_grace_seconds 应被 clamp 到 600");
}

void TestServerConfigInvalidJsonFallback() {
  TempWorkspace ws;
  ScopedCurrentPath cwd(ws.Root());
  WriteFile(ws.ConfigPath("server_config.json"), R"json({"tcp_port":7777)json");

  ServerConfig cfg;
  const bool loaded = LoadServerConfig(&cfg);
  Expect(!loaded, "server_config 非法 JSON 应返回 false");
  Expect(cfg.tcp_port == 7777, "非法 JSON 时应使用默认配置");
}

void TestPlayerRolesNegativeInputs() {
  TempWorkspace ws;
  ScopedCurrentPath cwd(ws.Root());
  WriteFile(ws.ConfigPath("player_roles.json"),
            R"json({
  "default_role_id": "oops",
  "roles": [
    123,
    {"role_id": "bad", "name": "x"},
    {
      "role_id": 7,
      "name": "",
      "max_health": 0,
      "attack": 5.5,
      "attack_speed": 0,
      "move_speed": "fast",
      "critical_hit_rate": 2000
    }
  ]
})json");

  PlayerRolesConfig cfg;
  const bool loaded = LoadPlayerRolesConfig(&cfg);
  Expect(loaded, "player_roles 应该加载成功");
  Expect(cfg.roles.size() == 1, "应只解析出一个有效职业");
  Expect(cfg.default_role_id == 7, "default_role_id 应回退到可用职业");
  const auto it = cfg.roles.find(7);
  Expect(it != cfg.roles.end(), "应存在 role_id=7");
  Expect(it->second.name == "职业7", "空名称应回退");
  Expect(it->second.max_health == 1, "max_health 应 clamp 到最小值");
  Expect(it->second.attack == 10, "attack 非整数应保留默认值");
  Expect(it->second.attack_speed == 1, "attack_speed 应 clamp 到最小值");
  ExpectNear(it->second.move_speed, 0.0f, 1e-4f,
             "move_speed 类型错误应保留默认值");
  Expect(it->second.critical_hit_rate == 1000,
         "critical_hit_rate 应 clamp 到 1000");
}

void TestPlayerRolesInvalidJsonFallback() {
  TempWorkspace ws;
  ScopedCurrentPath cwd(ws.Root());
  WriteFile(ws.ConfigPath("player_roles.json"), R"json({"roles":[)json");

  PlayerRolesConfig cfg;
  const bool loaded = LoadPlayerRolesConfig(&cfg);
  Expect(!loaded, "player_roles 非法 JSON 应返回 false");
  Expect(!cfg.roles.empty(), "非法 JSON 时应回退默认职业配置");
}

void TestEnemyTypesNegativeInputs() {
  TempWorkspace ws;
  ScopedCurrentPath cwd(ws.Root());
  WriteFile(ws.ConfigPath("enemy_types.json"),
            R"json({
  "default_type_id": 999,
  "enemies": [
    "bad",
    {
      "type_id": 2,
      "name": "",
      "max_health": 0,
      "move_speed": "fast",
      "damage": -5,
      "exp_reward": 10.1,
      "drop_chance": 150,
      "attack_enter_radius": 50,
      "attack_exit_radius": 20,
      "attack_interval_seconds": 0.01
    }
  ]
})json");

  EnemyTypesConfig cfg;
  const bool loaded = LoadEnemyTypesConfig(&cfg);
  Expect(loaded, "enemy_types 应该加载成功");
  Expect(cfg.enemies.size() == 1, "应只解析出一个有效敌人类型");
  Expect(cfg.default_type_id == 2, "default_type_id 应回退到可用类型");
  Expect(cfg.spawn_type_ids.size() == 1 && cfg.spawn_type_ids.front() == 2,
         "spawn_type_ids 应只包含有效类型");

  const auto it = cfg.enemies.find(2);
  Expect(it != cfg.enemies.end(), "应存在 type_id=2");
  Expect(it->second.name == "敌人2", "空名称应回退");
  Expect(it->second.max_health == 1, "max_health 应 clamp 到最小值");
  ExpectNear(it->second.move_speed, 60.0f, 1e-4f,
             "move_speed 类型错误应保留默认值");
  Expect(it->second.damage == 0, "damage 负数应保留默认值");
  Expect(it->second.exp_reward == 10, "exp_reward 非整数应保留默认值");
  Expect(it->second.drop_chance == 100, "drop_chance 应 clamp 到 100");
  ExpectNear(it->second.attack_enter_radius, 50.0f, 1e-4f,
             "attack_enter_radius 应按配置生效");
  ExpectNear(it->second.attack_exit_radius, 50.0f, 1e-4f,
             "attack_exit_radius 小于 enter 时应提升到 enter");
  ExpectNear(it->second.attack_interval_seconds, 0.05f, 1e-4f,
             "attack_interval_seconds 应 clamp 到最小值");
}

void TestEnemyTypesInvalidJsonFallback() {
  TempWorkspace ws;
  ScopedCurrentPath cwd(ws.Root());
  WriteFile(ws.ConfigPath("enemy_types.json"), R"json({"enemies":[)json");

  EnemyTypesConfig cfg;
  const bool loaded = LoadEnemyTypesConfig(&cfg);
  Expect(!loaded, "enemy_types 非法 JSON 应返回 false");
  Expect(!cfg.enemies.empty(), "非法 JSON 时应回退默认敌人配置");
}

void TestItemsNegativeInputs() {
  TempWorkspace ws;
  ScopedCurrentPath cwd(ws.Root());
  WriteFile(ws.ConfigPath("items_config.json"),
            R"json({
  "default_type_id": 2.2,
  "max_items_alive": 0,
  "pick_radius": 1000,
  "items": [
    "bad",
    {
      "type_id": 3,
      "name": "",
      "effect": "",
      "value": -1,
      "drop_weight": -5
    }
  ]
})json");

  ItemsConfig cfg;
  const bool loaded = LoadItemsConfig(&cfg);
  Expect(loaded, "items_config 应该加载成功");
  Expect(cfg.items.size() == 1, "应只解析出一个有效道具类型");
  Expect(cfg.default_type_id == 3, "default_type_id 应回退到可用道具");
  Expect(cfg.max_items_alive == 1, "max_items_alive 应 clamp 到最小值");
  ExpectNear(cfg.pick_radius, 500.0f, 1e-4f, "pick_radius 应 clamp 到最大值");
  const auto it = cfg.items.find(3);
  Expect(it != cfg.items.end(), "应存在 type_id=3");
  Expect(it->second.name == "道具3", "空名称应回退");
  Expect(it->second.effect == "none", "空 effect 应回退为 none");
  Expect(it->second.value == 0, "负数 value 应保留默认值 0");
  Expect(it->second.drop_weight == 0, "负数 drop_weight 应保留默认值 0");
}

void TestItemsInvalidJsonFallback() {
  TempWorkspace ws;
  ScopedCurrentPath cwd(ws.Root());
  WriteFile(ws.ConfigPath("items_config.json"), R"json({"items":[)json");

  ItemsConfig cfg;
  const bool loaded = LoadItemsConfig(&cfg);
  Expect(!loaded, "items_config 非法 JSON 应返回 false");
  Expect(!cfg.items.empty(), "非法 JSON 时应回退默认道具配置");
}

void TestUpgradeNegativeInputs() {
  TempWorkspace ws;
  ScopedCurrentPath cwd(ws.Root());
  WriteFile(ws.ConfigPath("upgrade_config.json"),
            R"json({
  "option_count": "x",
  "refresh_limit": -5,
  "upgrades": [
    {},
    {"type": "unknown", "level": "low", "value": 1, "weight": 1},
    {"type": "attack", "level": "medium", "value": 9999999, "weight": 0},
    123
  ]
})json");

  UpgradeConfig cfg;
  const bool loaded = LoadUpgradeConfig(&cfg);
  Expect(loaded, "upgrade_config 应该加载成功");
  Expect(cfg.option_count == 3, "option_count 应保持固定为 3");
  Expect(cfg.refresh_limit == 1,
         "refresh_limit 非法输入时应保留默认值并 clamp");
  Expect(cfg.effects.size() == 1, "应只解析出一个有效升级项");
  Expect(cfg.effects.front().type == lawnmower::UPGRADE_TYPE_ATTACK,
         "升级类型应解析为 attack");
  Expect(cfg.effects.front().level == lawnmower::UPGRADE_LEVEL_MEDIUM,
         "升级等级应解析为 medium");
  ExpectNear(cfg.effects.front().value, 100000.0f, 1e-3f,
             "升级值应 clamp 到最大值");
  Expect(cfg.effects.front().weight == 1, "权重应 clamp 到最小值 1");
}

void TestUpgradeInvalidJsonFallback() {
  TempWorkspace ws;
  ScopedCurrentPath cwd(ws.Root());
  WriteFile(ws.ConfigPath("upgrade_config.json"), R"json({"upgrades":[)json");

  UpgradeConfig cfg;
  const bool loaded = LoadUpgradeConfig(&cfg);
  Expect(!loaded, "upgrade_config 非法 JSON 应返回 false");
  Expect(!cfg.effects.empty(), "非法 JSON 时应回退默认升级配置");
}

void RunAll() {
  const std::vector<std::pair<std::string, std::function<void()>>> tests = {
      {"server_config_type_and_range_guards",
       TestServerConfigTypeAndRangeGuards},
      {"server_config_invalid_json_fallback",
       TestServerConfigInvalidJsonFallback},
      {"player_roles_negative_inputs", TestPlayerRolesNegativeInputs},
      {"player_roles_invalid_json_fallback",
       TestPlayerRolesInvalidJsonFallback},
      {"enemy_types_negative_inputs", TestEnemyTypesNegativeInputs},
      {"enemy_types_invalid_json_fallback", TestEnemyTypesInvalidJsonFallback},
      {"items_negative_inputs", TestItemsNegativeInputs},
      {"items_invalid_json_fallback", TestItemsInvalidJsonFallback},
      {"upgrade_negative_inputs", TestUpgradeNegativeInputs},
      {"upgrade_invalid_json_fallback", TestUpgradeInvalidJsonFallback},
  };

  for (const auto& [name, fn] : tests) {
    fn();
    std::cout << "[PASS] " << name << "\n";
  }
}
}  // namespace

int main() {
  try {
    RunAll();
    std::cout << "config_loader_smoke_test: PASS\n";
    return 0;
  } catch (const std::exception& ex) {
    std::cerr << "config_loader_smoke_test: FAIL: " << ex.what() << "\n";
    return 1;
  }
}
