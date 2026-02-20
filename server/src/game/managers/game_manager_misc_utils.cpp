#include "internal/game_manager_misc_utils.hpp"

#include <cmath>
#include <numbers>
#include <unordered_set>

namespace {
constexpr float kDirectionEpsilonSq =
    1e-6f;  // 方向向量长度平方的极小阈值，小于此视为无效输入
}  // namespace

namespace game_manager_misc_utils {

void DedupProjectileSpawns(std::vector<lawnmower::ProjectileState>* spawns) {
  if (spawns == nullptr || spawns->size() < 2) {
    return;
  }
  std::unordered_set<uint32_t> seen;
  seen.reserve(spawns->size());
  std::size_t out = 0;
  for (std::size_t i = 0; i < spawns->size(); ++i) {
    auto& spawn = (*spawns)[i];
    const uint32_t id = spawn.projectile_id();
    if (!seen.insert(id).second) {
      continue;
    }
    if (out != i) {
      (*spawns)[out] = std::move(spawn);
    }
    out += 1;
  }
  spawns->resize(out);
}

void DedupProjectileDespawns(
    std::vector<lawnmower::ProjectileDespawn>* despawns) {
  if (despawns == nullptr || despawns->size() < 2) {
    return;
  }
  std::unordered_set<uint32_t> seen;
  seen.reserve(despawns->size());
  std::size_t out = 0;
  for (std::size_t i = 0; i < despawns->size(); ++i) {
    auto& despawn = (*despawns)[i];
    const uint32_t id = despawn.projectile_id();
    if (!seen.insert(id).second) {
      continue;
    }
    if (out != i) {
      (*despawns)[out] = std::move(despawn);
    }
    out += 1;
  }
  despawns->resize(out);
}

lawnmower::ItemEffectType ResolveItemEffectType(std::string_view effect) {
  if (effect == "heal") {
    return lawnmower::ITEM_EFFECT_HEAL;
  }
  if (effect == "exp") {
    return lawnmower::ITEM_EFFECT_EXP;
  }
  if (effect == "speed") {
    return lawnmower::ITEM_EFFECT_SPEED;
  }
  return lawnmower::ITEM_EFFECT_NONE;
}

float DegreesFromDirection(float x, float y) {
  if (std::abs(x) < kDirectionEpsilonSq && std::abs(y) < kDirectionEpsilonSq) {
    return 0.0f;
  }
  const float angle_rad = std::atan2(y, x);
  return angle_rad * 180.0f / std::numbers::pi_v<float>;
}

}  // namespace game_manager_misc_utils
