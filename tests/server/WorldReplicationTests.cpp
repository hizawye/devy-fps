#include "server/WorldReplication.h"

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <vector>

namespace devy::server {
namespace {

bool contains_coord(const std::vector<ChunkRevisionEntry>& entries, int x, int y, int z) {
  return std::any_of(entries.begin(), entries.end(), [x, y, z](const ChunkRevisionEntry& entry) {
    return entry.coord.x == x && entry.coord.y == y && entry.coord.z == z;
  });
}

TEST_CASE("World replication sends cold-join chunk subscriptions once") {
  devy::voxel::World world;
  world.generate(2, 2, 32);

  WorldReplication replication({2, 1, 2, 0});

  const auto first = replication.build_player_update(10U, 1.0F, 1.0F, world);
  REQUIRE(first.player_id == 10U);
  REQUIRE(first.added.size() == 1U);
  REQUIRE(first.removed.empty());
  REQUIRE(first.deltas.empty());
  REQUIRE(first.added.front().coord.x == 0);
  REQUIRE(first.added.front().coord.y == 0);
  REQUIRE(first.added.front().coord.z == 0);

  const auto second = replication.build_player_update(10U, 1.0F, 1.0F, world);
  REQUIRE(second.added.empty());
  REQUIRE(second.removed.empty());
  REQUIRE(second.deltas.empty());
}

TEST_CASE("World replication emits chunk deltas when revisions advance") {
  devy::voxel::World world;
  world.generate(2, 2, 32);

  WorldReplication replication({2, 1, 2, 0});

  const auto baseline = replication.build_player_update(4U, 1.0F, 1.0F, world);
  REQUIRE(baseline.added.size() == 1U);

  replication.mark_chunk_dirty({0, 0, 0});
  const auto delta = replication.build_player_update(4U, 1.0F, 1.0F, world);
  REQUIRE(delta.added.empty());
  REQUIRE(delta.removed.empty());
  REQUIRE(delta.deltas.size() == 1U);
  REQUIRE(delta.deltas.front().coord.x == 0);
  REQUIRE(delta.deltas.front().coord.y == 0);
  REQUIRE(delta.deltas.front().coord.z == 0);
  REQUIRE(delta.deltas.front().revision == 1U);
}

TEST_CASE("World replication tracks subscription churn as players move across chunks") {
  devy::voxel::World world;
  world.generate(3, 1, 32);

  WorldReplication replication({3, 1, 1, 0});

  const auto initial = replication.build_player_update(3U, 1.0F, 1.0F, world);
  REQUIRE(initial.added.size() == 1U);
  REQUIRE(contains_coord(initial.added, 0, 0, 0));

  const auto moved_once = replication.build_player_update(3U, 40.0F, 1.0F, world);
  REQUIRE(moved_once.added.size() == 1U);
  REQUIRE(moved_once.removed.size() == 1U);
  REQUIRE(contains_coord(moved_once.added, 1, 0, 0));
  REQUIRE(contains_coord(moved_once.removed, 0, 0, 0));

  const auto moved_twice = replication.build_player_update(3U, 72.0F, 1.0F, world);
  REQUIRE(moved_twice.added.size() == 1U);
  REQUIRE(moved_twice.removed.size() == 1U);
  REQUIRE(contains_coord(moved_twice.added, 2, 0, 0));
  REQUIRE(contains_coord(moved_twice.removed, 1, 0, 0));
}

TEST_CASE("World replication culls chunk relevance by distance radius") {
  devy::voxel::World world;
  world.generate(3, 3, 32);

  WorldReplication replication({3, 1, 3, 1});

  const auto update = replication.build_player_update(2U, 48.0F, 48.0F, world);
  REQUIRE(update.removed.empty());
  REQUIRE(update.deltas.empty());
  REQUIRE(update.added.size() == 5U);
  REQUIRE(contains_coord(update.added, 1, 0, 1));
  REQUIRE(contains_coord(update.added, 0, 0, 1));
  REQUIRE(contains_coord(update.added, 2, 0, 1));
  REQUIRE(contains_coord(update.added, 1, 0, 0));
  REQUIRE(contains_coord(update.added, 1, 0, 2));
}

} // namespace
} // namespace devy::server
