#include "shared/game/Weapons.h"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <filesystem>
#include <string>
#include <unordered_map>

namespace devy::game {
namespace {

std::string resolve_weapons_path() {
  std::filesystem::path probe = std::filesystem::current_path();
  for (int depth = 0; depth < 8; ++depth) {
    const auto candidate = probe / "config" / "weapons.json";
    if (std::filesystem::exists(candidate)) {
      return candidate.string();
    }
    if (!probe.has_parent_path()) {
      break;
    }
    probe = probe.parent_path();
  }
  return "config/weapons.json";
}

TEST_CASE("Weapon config definitions produce sane DPS values by tier") {
  const auto definitions = load_weapons(resolve_weapons_path());
  REQUIRE(definitions.size() >= 4U);

  std::unordered_map<std::string, WeaponDefinition> by_id{};
  by_id.reserve(definitions.size());
  for (const auto& definition : definitions) {
    by_id[definition.id] = definition;
    REQUIRE(definition.tier >= 1);
    REQUIRE(weapon_damage_per_shot(definition) > 0.0);
    REQUIRE(weapon_dps(definition) > 0.0);
  }

  REQUIRE(weapon_dps(by_id.at("pistol")) ==
          Catch::Approx(77.0).margin(0.0001));
  REQUIRE(weapon_dps(by_id.at("rifle")) ==
          Catch::Approx(195.0).margin(0.0001));
  REQUIRE(weapon_dps(by_id.at("shotgun")) ==
          Catch::Approx(76.8).margin(0.0001));
  REQUIRE(weapon_dps(by_id.at("railgun")) ==
          Catch::Approx(63.0).margin(0.0001));
}

} // namespace
} // namespace devy::game
