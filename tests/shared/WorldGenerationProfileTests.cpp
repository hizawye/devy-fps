#include "shared/voxel/World.h"
#include "shared/voxel/WorldGenerationProfile.h"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <cstdint>
#include <vector>

namespace devy::voxel {
namespace {

uint64_t world_content_digest(const World& world) {
  struct OrderedChunk {
    ChunkCoord coord{};
    const Chunk* chunk{nullptr};
  };

  std::vector<OrderedChunk> ordered{};
  ordered.reserve(world.chunks().size());
  for (const auto& entry : world.chunks()) {
    ordered.push_back({entry.first, &entry.second});
  }

  std::sort(ordered.begin(), ordered.end(), [](const OrderedChunk& lhs, const OrderedChunk& rhs) {
    if (lhs.coord.x != rhs.coord.x) {
      return lhs.coord.x < rhs.coord.x;
    }
    if (lhs.coord.y != rhs.coord.y) {
      return lhs.coord.y < rhs.coord.y;
    }
    return lhs.coord.z < rhs.coord.z;
  });

  uint64_t digest = 1469598103934665603ULL;
  const auto mix = [&digest](uint64_t value) {
    digest ^= value;
    digest *= 1099511628211ULL;
  };

  for (const auto& entry : ordered) {
    mix(static_cast<uint64_t>(static_cast<uint32_t>(entry.coord.x)));
    mix(static_cast<uint64_t>(static_cast<uint32_t>(entry.coord.y)));
    mix(static_cast<uint64_t>(static_cast<uint32_t>(entry.coord.z)));
    const Chunk* chunk = entry.chunk;
    if (chunk == nullptr) {
      continue;
    }

    for (int z = 0; z < kChunkSize; ++z) {
      for (int y = 0; y < kChunkSize; ++y) {
        for (int x = 0; x < kChunkSize; ++x) {
          mix(static_cast<uint64_t>(chunk->get(x, y, z)));
        }
      }
    }
  }

  return digest;
}

TEST_CASE("World generation profile JSON round-trips with full expansion payload") {
  WorldGenerationProfile profile{};
  profile.world_seed = 2026U;
  profile.expansion.enabled = true;
  profile.expansion.poi_density = PoiDensity::High;
  profile.expansion.cell_size_chunks = 3;
  profile.expansion.jitter_units = 17;
  profile.expansion.min_poi_spacing_units = 72;
  profile.expansion.poi_types = {true, false, true};
  profile.expansion.loot_bias_multiplier = 1.75F;

  const nlohmann::json encoded = world_generation_profile_to_json(profile);
  const auto decoded = world_generation_profile_from_json(encoded);

  REQUIRE(decoded.has_value());
  REQUIRE(decoded->world_seed == profile.world_seed);
  REQUIRE(decoded->expansion.enabled == profile.expansion.enabled);
  REQUIRE(decoded->expansion.poi_density == profile.expansion.poi_density);
  REQUIRE(decoded->expansion.cell_size_chunks == profile.expansion.cell_size_chunks);
  REQUIRE(decoded->expansion.jitter_units == profile.expansion.jitter_units);
  REQUIRE(decoded->expansion.min_poi_spacing_units ==
          profile.expansion.min_poi_spacing_units);
  REQUIRE(decoded->expansion.poi_types.outpost == profile.expansion.poi_types.outpost);
  REQUIRE(decoded->expansion.poi_types.ruins == profile.expansion.poi_types.ruins);
  REQUIRE(decoded->expansion.poi_types.loot_shrine == profile.expansion.poi_types.loot_shrine);
  REQUIRE(decoded->expansion.loot_bias_multiplier ==
          Catch::Approx(profile.expansion.loot_bias_multiplier));
}

TEST_CASE("World generation profile parser rejects invalid payload types and values") {
  SECTION("non-object payload is rejected") {
    REQUIRE_FALSE(world_generation_profile_from_json(nlohmann::json::array()).has_value());
  }

  SECTION("invalid poi density is rejected") {
    const nlohmann::json invalid = {{"poi_density", "extreme"}};
    REQUIRE_FALSE(world_generation_profile_from_json(invalid).has_value());
  }

  SECTION("invalid nested poi type toggle is rejected") {
    const nlohmann::json invalid = {{"poi_types", {{"outpost", "enabled"}}}};
    REQUIRE_FALSE(world_generation_profile_from_json(invalid).has_value());
  }

  SECTION("non-positive loot multiplier is rejected") {
    const nlohmann::json invalid = {{"loot_bias_multiplier", 0.0}};
    REQUIRE_FALSE(world_generation_profile_from_json(invalid).has_value());
  }
}

TEST_CASE("World generation profile sanitization restores safe defaults") {
  WorldGenerationProfile profile{};
  profile.expansion.cell_size_chunks = 0;
  profile.expansion.jitter_units = -9;
  profile.expansion.min_poi_spacing_units = -20;
  profile.expansion.poi_types = {false, false, false};
  profile.expansion.loot_bias_multiplier = 0.0F;

  const WorldGenerationProfile sanitized = sanitize_world_generation_profile(profile);
  REQUIRE(sanitized.expansion.cell_size_chunks == 4);
  REQUIRE(sanitized.expansion.jitter_units == 24);
  REQUIRE(sanitized.expansion.min_poi_spacing_units == 96);
  REQUIRE(sanitized.expansion.poi_types.outpost);
  REQUIRE_FALSE(sanitized.expansion.poi_types.ruins);
  REQUIRE_FALSE(sanitized.expansion.poi_types.loot_shrine);
  REQUIRE(sanitized.expansion.loot_bias_multiplier == Catch::Approx(1.25F));
}

TEST_CASE("World generation is deterministic for the same profile and seed") {
  WorldGenerationProfile profile{};
  profile.world_seed = 424242U;
  profile.expansion.enabled = true;
  profile.expansion.poi_density = PoiDensity::High;
  profile.expansion.cell_size_chunks = 2;
  profile.expansion.jitter_units = 12;
  profile.expansion.min_poi_spacing_units = 32;
  profile = sanitize_world_generation_profile(profile);

  World a{};
  World b{};
  a.generate(5, 5, 96, profile);
  b.generate(5, 5, 96, profile);

  REQUIRE(world_content_digest(a) == world_content_digest(b));
}

TEST_CASE("World generation differs when only the world seed changes") {
  WorldGenerationProfile profile_a{};
  profile_a.world_seed = 1337U;
  profile_a.expansion.enabled = true;
  profile_a.expansion.poi_density = PoiDensity::High;
  profile_a.expansion.cell_size_chunks = 1;
  profile_a.expansion.jitter_units = 15;
  profile_a.expansion.min_poi_spacing_units = 0;
  profile_a = sanitize_world_generation_profile(profile_a);

  WorldGenerationProfile profile_b = profile_a;
  profile_b.world_seed = 9001U;

  World a{};
  World b{};
  a.generate(5, 5, 96, profile_a);
  b.generate(5, 5, 96, profile_b);

  REQUIRE(world_content_digest(a) != world_content_digest(b));
}

} // namespace
} // namespace devy::voxel
