#include "shared/game/Weapons.h"

#include "shared/Config.h"

namespace devy::game {

std::vector<WeaponDefinition> load_weapons(const std::string& path) {
  auto root = devy::config::load_json(path);
  std::vector<WeaponDefinition> weapons;
  if (!root.contains("weapons")) {
    return weapons;
  }

  for (const auto& item : root["weapons"]) {
    WeaponDefinition def;
    def.id = item.value("id", "");
    def.type = item.value("type", "");
    def.damage = item.value("damage", 0);
    def.mag_size = item.value("mag_size", 0);
    def.rate_of_fire = item.value("rate_of_fire", 0.0f);
    def.pellets = item.value("pellets", 0);
    def.damage_per_pellet = item.value("damage_per_pellet", 0);
    def.tier = item.value("tier", 1);
    weapons.push_back(def);
  }

  return weapons;
}

} // namespace devy::game
