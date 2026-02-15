#include "server/CombatSimulation.h"

#include <catch2/catch_test_macros.hpp>

#include <chrono>
#include <cstdint>
#include <vector>

namespace devy::server {
namespace {

std::vector<devy::game::WeaponDefinition> weapon_fixture() {
  return {
      {"pistol", "hitscan", 22, 12, 3.5F, 0, 0, 72.0F, 0.0F, 1},
      {"rifle", "hitscan", 30, 30, 6.5F, 0, 0, 110.0F, 0.0F, 2},
      {"shotgun", "projectile", 0, 6, 1.2F, 8, 8, 45.0F, 35.0F, 2},
      {"railgun", "hitscan", 90, 4, 0.7F, 0, 0, 180.0F, 0.0F, 3},
  };
}

TEST_CASE("Combat simulation applies damage and emits lethal death events") {
  CombatSimulation simulation(weapon_fixture());
  simulation.ensure_player(1U);
  simulation.ensure_player(2U);
  simulation.set_player_position(1U, 0.0F, 0.0F);
  simulation.set_player_position(2U, 15.0F, 0.0F);

  REQUIRE(simulation.enqueue_fire({1U, 1U, "pistol", 0.0F, 0.0F, 1.0F, 0.0F, RuntimeTimePoint{}}) ==
          FireEnqueueStatus::Accepted);
  const auto tick_1 = simulation.resolve_tick(1U, std::chrono::milliseconds(100));
  REQUIRE(tick_1.damage_events.size() == 1U);
  REQUIRE(tick_1.damage_events.front().damage == 22);
  REQUIRE_FALSE(tick_1.damage_events.front().lethal);
  REQUIRE(tick_1.death_events.empty());

  const auto tick_2 = simulation.resolve_tick(2U, std::chrono::milliseconds(100));
  REQUIRE(tick_2.damage_events.empty());
  const auto tick_3 = simulation.resolve_tick(3U, std::chrono::milliseconds(100));
  REQUIRE(tick_3.damage_events.empty());

  REQUIRE(simulation.enqueue_fire({1U, 2U, "railgun", 0.0F, 0.0F, 1.0F, 0.0F, RuntimeTimePoint{}}) ==
          FireEnqueueStatus::Accepted);
  const auto tick_4 = simulation.resolve_tick(4U, std::chrono::milliseconds(100));
  REQUIRE(tick_4.damage_events.size() == 1U);
  REQUIRE(tick_4.damage_events.front().lethal);
  REQUIRE(tick_4.death_events.size() == 1U);
  REQUIRE(tick_4.death_events.front().killer_id == 1U);
  REQUIRE(tick_4.death_events.front().victim_id == 2U);

  const auto victim = simulation.state_for(2U);
  REQUIRE(victim.has_value());
  REQUIRE(victim->health == 0);
  REQUIRE_FALSE(victim->alive);
}

TEST_CASE("Combat simulation resolves same-tick fire in deterministic attacker order") {
  CombatSimulation simulation(weapon_fixture());
  simulation.ensure_player(1U);
  simulation.ensure_player(2U);
  simulation.ensure_player(3U);
  simulation.set_player_position(1U, 20.0F, 0.0F);
  simulation.set_player_position(2U, 0.0F, 5.0F);
  simulation.set_player_position(3U, 0.0F, -5.0F);

  REQUIRE(simulation.enqueue_fire(
              {2U, 1U, "pistol", 0.0F, 5.0F, 0.9701425F, -0.24253563F, RuntimeTimePoint{}}) ==
          FireEnqueueStatus::Accepted);
  REQUIRE(simulation.enqueue_fire(
              {3U, 1U, "railgun", 0.0F, -5.0F, 0.9701425F, 0.24253563F, RuntimeTimePoint{}}) ==
          FireEnqueueStatus::Accepted);

  const auto tick = simulation.resolve_tick(1U, std::chrono::milliseconds(100));
  REQUIRE(tick.damage_events.size() == 2U);
  REQUIRE(tick.damage_events[0].attacker_id == 2U);
  REQUIRE(tick.damage_events[0].damage == 22);
  REQUIRE_FALSE(tick.damage_events[0].lethal);
  REQUIRE(tick.damage_events[1].attacker_id == 3U);
  REQUIRE(tick.damage_events[1].damage == 90);
  REQUIRE(tick.damage_events[1].lethal);
  REQUIRE(tick.death_events.size() == 1U);
  REQUIRE(tick.death_events.front().killer_id == 3U);
}

TEST_CASE("Combat simulation delays projectile damage for high-latency travel windows") {
  CombatSimulation simulation(weapon_fixture());
  simulation.ensure_player(1U);
  simulation.ensure_player(2U);
  simulation.set_player_position(1U, 0.0F, 0.0F);
  simulation.set_player_position(2U, 30.0F, 0.0F);

  REQUIRE(simulation.enqueue_fire({1U, 1U, "shotgun", 0.0F, 0.0F, 1.0F, 0.0F, RuntimeTimePoint{}}) ==
          FireEnqueueStatus::Accepted);

  const auto tick_1 = simulation.resolve_tick(1U, std::chrono::milliseconds(50));
  REQUIRE(tick_1.damage_events.empty());

  uint64_t first_hit_tick = 0U;
  for (uint64_t tick = 2U; tick <= 40U; ++tick) {
    const auto result = simulation.resolve_tick(tick, std::chrono::milliseconds(50));
    if (!result.damage_events.empty()) {
      first_hit_tick = tick;
      REQUIRE(result.damage_events.front().damage == 64);
      REQUIRE(result.damage_events.front().victim_id == 2U);
      break;
    }
  }

  REQUIRE(first_hit_tick == 19U);
}

} // namespace
} // namespace devy::server
