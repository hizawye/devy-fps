#include "client/ChunkSyncApplier.h"

#include <catch2/catch_test_macros.hpp>

#include <unordered_map>

TEST_CASE("Chunk sync parser extracts added, removed, and delta revisions") {
  const nlohmann::json snapshot = {
      {"tick", 4},
      {"players", nlohmann::json::array()},
      {"chunk_sync",
       {{"added",
         nlohmann::json::array({{{"x", 0}, {"y", 0}, {"z", 0}, {"revision", 2}},
                                {{"x", 1}, {"y", 0}, {"z", 0}, {"revision", 3}}})},
        {"removed", nlohmann::json::array({{{"x", 2}, {"y", 0}, {"z", 0}, {"revision", 0}}})},
        {"deltas", nlohmann::json::array({{{"x", 0}, {"y", 1}, {"z", 0}, {"revision", 5}}})}}}};

  const auto parsed = devy::client::parse_chunk_sync(snapshot);
  REQUIRE(parsed.has_value());
  REQUIRE(parsed->added.size() == 2U);
  REQUIRE(parsed->removed.size() == 1U);
  REQUIRE(parsed->deltas.size() == 1U);
  REQUIRE(parsed->added[0].coord.x == 0);
  REQUIRE(parsed->added[0].revision == 2U);
  REQUIRE(parsed->removed[0].coord.x == 2);
  REQUIRE(parsed->deltas[0].coord.y == 1);
}

TEST_CASE("Chunk sync applier lazily materializes and removes world chunks") {
  devy::voxel::World world{};
  world.generate(0, 0, 64);

  std::unordered_map<devy::voxel::ChunkCoord, uint32_t, devy::voxel::ChunkCoordHash> revisions{};

  devy::client::ChunkSyncDelta first{};
  first.added.push_back({{0, 0, 0}, 1U});
  const auto first_result = devy::client::apply_chunk_sync(&world, 64, first, &revisions);
  REQUIRE(first_result.changed);
  REQUIRE(first_result.added_or_updated == 1U);
  REQUIRE(world.get_chunk(0, 0, 0) != nullptr);

  const auto second_result = devy::client::apply_chunk_sync(&world, 64, first, &revisions);
  REQUIRE_FALSE(second_result.changed);
  REQUIRE(second_result.added_or_updated == 0U);

  devy::client::ChunkSyncDelta delta_update{};
  delta_update.deltas.push_back({{0, 0, 0}, 2U});
  const auto update_result =
      devy::client::apply_chunk_sync(&world, 64, delta_update, &revisions);
  REQUIRE(update_result.changed);
  REQUIRE(update_result.added_or_updated == 1U);

  devy::client::ChunkSyncDelta removal{};
  removal.removed.push_back({{0, 0, 0}, 0U});
  const auto removal_result = devy::client::apply_chunk_sync(&world, 64, removal, &revisions);
  REQUIRE(removal_result.changed);
  REQUIRE(removal_result.removed == 1U);
  REQUIRE(world.get_chunk(0, 0, 0) == nullptr);
}
