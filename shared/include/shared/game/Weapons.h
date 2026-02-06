#pragma once

#include <string>
#include <vector>

namespace devy::game {

struct WeaponDefinition {
  std::string id;
  std::string type;
  int damage = 0;
  int mag_size = 0;
  float rate_of_fire = 0.0f;
  int pellets = 0;
  int damage_per_pellet = 0;
  int tier = 1;
};

std::vector<WeaponDefinition> load_weapons(const std::string& path);

} // namespace devy::game
