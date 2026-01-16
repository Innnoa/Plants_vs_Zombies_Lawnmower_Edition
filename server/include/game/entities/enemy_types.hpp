#pragma once

#include <array>
#include <cstdint>
#include <string_view>

struct EnemyType {
  uint32_t type_id = 0;
  std::string_view name;
  int32_t max_health = 0;
  float move_speed = 0.0f;
  int32_t damage = 0;
  int32_t exp_reward = 0;
};

constexpr std::array<EnemyType, 4> kEnemyTypes = {{
    {1, "普通僵尸", 30, 60.0f, 0, 10},
    {2, "路障僵尸", 60, 50.0f, 0, 20},
    {3, "铁桶僵尸", 120, 40.0f, 0, 40},
    {4, "橄榄球僵尸", 80, 100.0f, 0, 50},
}};

inline const EnemyType& DefaultEnemyType() { return kEnemyTypes.front(); }

inline const EnemyType* FindEnemyType(uint32_t type_id) {
  for (const auto& type : kEnemyTypes) {
    if (type.type_id == type_id) {
      return &type;
    }
  }
  return nullptr;
}
