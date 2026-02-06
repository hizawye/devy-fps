#include "shared/game/Treasure.h"

#include "shared/Config.h"

namespace devy::game {

std::vector<TreasureDefinition> load_treasures(const std::string& path) {
  auto root = devy::config::load_json(path);
  std::vector<TreasureDefinition> treasures;
  if (!root.contains("treasures")) {
    return treasures;
  }

  for (const auto& item : root["treasures"]) {
    TreasureDefinition def;
    def.id = item.value("id", "");
    def.value = item.value("value", 0);
    def.weight = item.value("weight", 0);
    def.rarity = item.value("rarity", "");
    treasures.push_back(def);
  }

  return treasures;
}

} // namespace devy::game
