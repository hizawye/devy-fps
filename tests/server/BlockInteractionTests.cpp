#include "server/BlockInteraction.h"

#include <catch2/catch_test_macros.hpp>

namespace devy::server {
namespace {

TEST_CASE("Block interaction applies break requests against non-air blocks") {
  devy::voxel::World world;
  world.generate(1, 1, 32);

  BlockInteraction interaction;
  const auto outcome = interaction.apply({1U, 1, 0, 1, 0U}, world);
  REQUIRE(outcome.applied());
  REQUIRE(outcome.previous_block_id != 0U);
  REQUIRE(outcome.chunk_x == 0);
  REQUIRE(outcome.chunk_y == 0);
  REQUIRE(outcome.chunk_z == 0);

  const auto updated_block = world.block_at(1, 0, 1);
  REQUIRE(updated_block.has_value());
  REQUIRE(updated_block.value() == 0U);
}

TEST_CASE("Block interaction enforces place and break conflict rules") {
  devy::voxel::World world;
  world.generate(1, 1, 32);
  REQUIRE(world.set_block(2, 20, 2, 0U));

  BlockInteraction interaction;

  SECTION("placing in occupied cell is rejected") {
    const auto outcome = interaction.apply({1U, 2, 0, 2, 1U}, world);
    REQUIRE(outcome.status == BlockUpdateStatus::ConflictAlreadyOccupied);
  }

  SECTION("placing identical block id is treated as no-op") {
    const auto original = world.block_at(2, 0, 2);
    REQUIRE(original.has_value());

    const auto outcome = interaction.apply({1U, 2, 0, 2, original.value()}, world);
    REQUIRE(outcome.status == BlockUpdateStatus::NoChange);
  }

  SECTION("breaking an empty cell is rejected") {
    const auto outcome = interaction.apply({1U, 2, 20, 2, 0U}, world);
    REQUIRE(outcome.status == BlockUpdateStatus::ConflictAlreadyEmpty);
  }

  SECTION("placing in empty cell succeeds") {
    const auto outcome = interaction.apply({1U, 2, 20, 2, 2U}, world);
    REQUIRE(outcome.applied());
    const auto block = world.block_at(2, 20, 2);
    REQUIRE(block.has_value());
    REQUIRE(block.value() == 2U);
  }
}

TEST_CASE("Block interaction rejects invalid ids and out-of-bounds updates") {
  devy::voxel::World world;
  world.generate(1, 1, 32);

  BlockInteraction interaction;
  const auto invalid_id = interaction.apply({1U, 0, 0, 0, 99U}, world);
  REQUIRE(invalid_id.status == BlockUpdateStatus::InvalidBlockId);

  const auto out_of_bounds = interaction.apply({1U, 0, 32, 0, 0U}, world);
  REQUIRE(out_of_bounds.status == BlockUpdateStatus::OutOfBounds);
}

TEST_CASE("Block interaction resolves same-block races with first writer winning") {
  devy::voxel::World world;
  world.generate(1, 1, 32);
  REQUIRE(world.set_block(5, 20, 5, 0U));

  BlockInteraction interaction;
  const auto first = interaction.apply({11U, 5, 20, 5, 1U}, world);
  REQUIRE(first.status == BlockUpdateStatus::Applied);

  const auto second = interaction.apply({12U, 5, 20, 5, 2U}, world);
  REQUIRE(second.status == BlockUpdateStatus::ConflictAlreadyOccupied);

  const auto block = world.block_at(5, 20, 5);
  REQUIRE(block.has_value());
  REQUIRE(block.value() == 1U);
}

} // namespace
} // namespace devy::server
