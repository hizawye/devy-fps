#include "shared/game/Weapons.h"

#include "shared/Config.h"

#include <algorithm>
#include <cctype>

namespace devy::game {
namespace {

std::string lowercase(std::string value) {
  std::transform(value.begin(), value.end(), value.begin(),
                 [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
  return value;
}

} // namespace

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
    def.range_units = item.value("range_units", 0.0f);
    def.projectile_speed_units_per_second =
        item.value("projectile_speed_units_per_second", 0.0f);
    def.tier = item.value("tier", 1);
    weapons.push_back(def);
  }

  return weapons;
}

double weapon_damage_per_shot(const WeaponDefinition& weapon) {
  const std::string type = lowercase(weapon.type);
  if (type == "projectile") {
    if (weapon.pellets > 0 && weapon.damage_per_pellet > 0) {
      return static_cast<double>(weapon.pellets) * static_cast<double>(weapon.damage_per_pellet);
    }
  }

  if (weapon.damage > 0) {
    return static_cast<double>(weapon.damage);
  }
  return 0.0;
}

double weapon_dps(const WeaponDefinition& weapon) {
  if (weapon.rate_of_fire <= 0.0f) {
    return 0.0;
  }
  return weapon_damage_per_shot(weapon) * static_cast<double>(weapon.rate_of_fire);
}

} // namespace devy::game
