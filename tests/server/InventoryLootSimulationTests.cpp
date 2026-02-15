#include "server/InventoryLootSimulation.h"

#include <catch2/catch_test_macros.hpp>

#include <cstdint>
#include <vector>

namespace devy::server {
namespace {

std::vector<devy::game::TreasureDefinition> loot_fixture() {
  return {{"coins", 10, 1, "common"}, {"relic", 100, 4, "rare"}};
}

std::vector<PlayerMotionState> make_motion(uint32_t player_id, float x, float y) {
  return {{player_id, x, y, 0.0F, 0.0F, 0U}};
}

std::vector<PlayerCombatState> make_alive(uint32_t player_id, bool alive = true) {
  return {{player_id, 100, alive, 0U}};
}

TEST_CASE("Inventory loot scheduled spawns are deterministic and bounded") {
  InventoryLootSimulation simulation(
      loot_fixture(),
      {2U, 2U, 64U, 2.5F, 8U, 32, LootDropMode::All, 64, 64, 0.75F});

  const auto tick_1 = simulation.resolve_tick(1U, {}, {}, {});
  REQUIRE(tick_1.spawned.empty());

  const auto tick_2 = simulation.resolve_tick(2U, {}, {}, {});
  REQUIRE(tick_2.spawned.size() == 1U);
  REQUIRE(tick_2.spawned.front().spawn_id == 1U);
  REQUIRE(tick_2.spawned.front().treasure_id == "coins");
  REQUIRE(tick_2.spawned.front().source == TreasureSpawnSource::Scheduled);

  const auto tick_4 = simulation.resolve_tick(4U, {}, {}, {});
  REQUIRE(tick_4.spawned.size() == 1U);
  REQUIRE(tick_4.spawned.front().spawn_id == 2U);
  REQUIRE(tick_4.spawned.front().treasure_id == "relic");

  const auto tick_6 = simulation.resolve_tick(6U, {}, {}, {});
  REQUIRE(tick_6.spawned.empty());

  const auto active = simulation.active_spawns();
  REQUIRE(active.size() == 2U);
  REQUIRE(active[0].spawn_id == 1U);
  REQUIRE(active[1].spawn_id == 2U);
}

TEST_CASE("Inventory loot rejects duplicate and over-capacity pickup requests") {
  InventoryLootSimulation simulation(
      loot_fixture(),
      {1U, 8U, 64U, 1.0F, 1U, 64, LootDropMode::All, 64, 64, 0.75F});

  simulation.ensure_player(1U);

  const auto spawn_tick = simulation.resolve_tick(1U, make_motion(1U, 54.5F, 18.5F), make_alive(1U), {});
  REQUIRE(spawn_tick.spawned.size() == 1U);
  REQUIRE(spawn_tick.spawned.front().spawn_id == 1U);

  REQUIRE(simulation.enqueue_pickup({1U, 1U, 1U, RuntimeTimePoint{}}) ==
          TreasurePickupEnqueueStatus::Accepted);
  const auto collect_tick =
      simulation.resolve_tick(2U, make_motion(1U, 54.5F, 18.5F), make_alive(1U), {});
  REQUIRE(collect_tick.pickup_results.size() == 1U);
  REQUIRE(collect_tick.pickup_results.front().status == TreasurePickupResolveStatus::Collected);
  REQUIRE(collect_tick.inventory_deltas.size() == 1U);
  REQUIRE(collect_tick.inventory_deltas.front().player_id == 1U);
  REQUIRE(collect_tick.inventory_deltas.front().item_count == 1U);

  REQUIRE(simulation.enqueue_pickup({1U, 2U, 1U, RuntimeTimePoint{}}) ==
          TreasurePickupEnqueueStatus::Accepted);
  const auto duplicate_tick =
      simulation.resolve_tick(3U, make_motion(1U, 27.5F, 7.5F), make_alive(1U), {});
  REQUIRE(duplicate_tick.pickup_results.size() == 1U);
  REQUIRE(duplicate_tick.pickup_results.front().status ==
          TreasurePickupResolveStatus::DuplicatePickup);

  REQUIRE(simulation.enqueue_pickup({1U, 3U, 2U, RuntimeTimePoint{}}) ==
          TreasurePickupEnqueueStatus::Accepted);
  const auto capacity_tick =
      simulation.resolve_tick(4U, make_motion(1U, 27.5F, 7.5F), make_alive(1U), {});
  REQUIRE(capacity_tick.pickup_results.size() == 1U);
  REQUIRE(capacity_tick.pickup_results.front().status ==
          TreasurePickupResolveStatus::InventoryCapacityExceeded);
}

TEST_CASE("Inventory loot drop-on-death clears victim inventory and respawns loot") {
  InventoryLootSimulation simulation(
      loot_fixture(),
      {100U, 8U, 64U, 1.0F, 8U, 64, LootDropMode::All, 64, 64, 0.75F});

  simulation.ensure_player(1U);
  simulation.ensure_player(2U);

  const auto spawn_tick = simulation.resolve_tick(100U, make_motion(2U, 54.5F, 18.5F), make_alive(2U), {});
  REQUIRE(spawn_tick.spawned.size() == 1U);
  REQUIRE(spawn_tick.spawned.front().spawn_id == 1U);

  REQUIRE(simulation.enqueue_pickup({2U, 1U, 1U, RuntimeTimePoint{}}) ==
          TreasurePickupEnqueueStatus::Accepted);
  const auto collect_tick =
      simulation.resolve_tick(101U, make_motion(2U, 54.5F, 18.5F), make_alive(2U), {});
  REQUIRE(collect_tick.pickup_results.size() == 1U);
  REQUIRE(collect_tick.pickup_results.front().status == TreasurePickupResolveStatus::Collected);

  const auto drop_tick = simulation.resolve_tick(
      102U, make_motion(2U, 20.0F, 8.0F), make_alive(2U, false), {{102U, 2U, 1U, 9U, "railgun"}});
  REQUIRE(drop_tick.spawned.size() == 1U);
  REQUIRE(drop_tick.spawned.front().source == TreasureSpawnSource::DeathDrop);
  REQUIRE(drop_tick.spawned.front().treasure_id == "coins");
  REQUIRE(drop_tick.inventory_deltas.size() == 1U);
  REQUIRE(drop_tick.inventory_deltas.front().player_id == 2U);
  REQUIRE(drop_tick.inventory_deltas.front().item_count == 0U);
  REQUIRE(drop_tick.inventory_deltas.front().total_value == 0);
}

} // namespace
} // namespace devy::server
