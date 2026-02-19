#pragma once

#include <cstdint>
#include <optional>
#include <string_view>

#include <nlohmann/json.hpp>

namespace devy::voxel {

enum class PoiDensity : uint8_t {
  Low = 0,
  Medium,
  High
};

struct PoiTypeToggles {
  bool outpost{true};
  bool ruins{true};
  bool loot_shrine{true};
};

struct WorldExpansionProfile {
  bool enabled{true};
  PoiDensity poi_density{PoiDensity::Medium};
  int cell_size_chunks{4};
  int jitter_units{24};
  int min_poi_spacing_units{96};
  PoiTypeToggles poi_types{};
  float loot_bias_multiplier{1.25F};
};

struct WorldGenerationProfile {
  uint32_t world_seed{1337U};
  WorldExpansionProfile expansion{};
};

[[nodiscard]] const char* to_string(PoiDensity density);
[[nodiscard]] std::optional<PoiDensity> parse_poi_density(std::string_view value);
[[nodiscard]] WorldGenerationProfile sanitize_world_generation_profile(
    WorldGenerationProfile profile);
[[nodiscard]] nlohmann::json world_generation_profile_to_json(
    const WorldGenerationProfile& profile);
[[nodiscard]] std::optional<WorldGenerationProfile> world_generation_profile_from_json(
    const nlohmann::json& value);
[[nodiscard]] float poi_density_spawn_probability(PoiDensity density);

} // namespace devy::voxel
