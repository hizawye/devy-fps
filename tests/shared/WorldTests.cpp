#include "shared/voxel/World.h"

#include <catch2/catch_test_macros.hpp>

#include <cstddef>
#include <utility>
#include <vector>

namespace devy::voxel {
namespace {

TEST_CASE("World generation progress callback is monotonic and completes at total chunks") {
  World world{};
  std::vector<std::pair<std::size_t, std::size_t>> progress_samples{};

  world.generate(2, 2, 32,
                 [&](std::size_t generated, std::size_t total) {
                   progress_samples.emplace_back(generated, total);
                 });

  REQUIRE_FALSE(progress_samples.empty());

  const std::size_t total_chunks = progress_samples.front().second;
  REQUIRE(total_chunks == 4U);
  REQUIRE(progress_samples.front().first == 0U);

  std::size_t previous = 0U;
  for (const auto& sample : progress_samples) {
    REQUIRE(sample.second == total_chunks);
    REQUIRE(sample.first >= previous);
    REQUIRE(sample.first <= total_chunks);
    previous = sample.first;
  }

  REQUIRE(progress_samples.back().first == total_chunks);
  REQUIRE(world.chunks().size() == total_chunks);
}

TEST_CASE("World generation reports zero total for empty dimensions") {
  World world{};
  std::vector<std::pair<std::size_t, std::size_t>> progress_samples{};

  world.generate(0, 0, 64,
                 [&](std::size_t generated, std::size_t total) {
                   progress_samples.emplace_back(generated, total);
                 });

  REQUIRE(progress_samples.size() == 1U);
  REQUIRE(progress_samples.front().first == 0U);
  REQUIRE(progress_samples.front().second == 0U);
  REQUIRE(world.chunks().empty());
}

} // namespace
} // namespace devy::voxel
