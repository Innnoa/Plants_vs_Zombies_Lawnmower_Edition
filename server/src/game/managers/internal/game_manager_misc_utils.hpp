#pragma once

#include <string_view>
#include <vector>

#include "message.pb.h"

namespace game_manager_misc_utils {

void DedupProjectileSpawns(std::vector<lawnmower::ProjectileState>* spawns);

void DedupProjectileDespawns(
    std::vector<lawnmower::ProjectileDespawn>* despawns);

lawnmower::ItemEffectType ResolveItemEffectType(std::string_view effect);

float DegreesFromDirection(float x, float y);

}  // namespace game_manager_misc_utils
