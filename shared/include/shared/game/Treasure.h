#pragma once

#include <string>
#include <vector>

namespace devy::game {

struct TreasureDefinition {
  std::string id;
  int value = 0;
  int weight = 0;
  std::string rarity;
};

std::vector<TreasureDefinition> load_treasures(const std::string& path);

} // namespace devy::game
