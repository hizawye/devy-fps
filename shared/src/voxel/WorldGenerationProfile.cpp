#include "shared/voxel/WorldGenerationProfile.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <limits>
#include <string>

namespace devy::voxel {
namespace {

constexpr int kDefaultCellSizeChunks = 4;
constexpr int kDefaultJitterUnits = 24;
constexpr int kDefaultMinPoiSpacingUnits = 96;
constexpr float kDefaultLootBiasMultiplier = 1.25F;

std::string lowercase_string(std::string value) {
  std::transform(value.begin(), value.end(), value.begin(), [](unsigned char c) {
    return static_cast<char>(std::tolower(c));
  });
  return value;
}

std::optional<int64_t> json_to_i64(const nlohmann::json& value) {
  if (value.is_number_integer()) {
    return value.get<int64_t>();
  }
  if (value.is_number_unsigned()) {
    const uint64_t raw = value.get<uint64_t>();
    if (raw > static_cast<uint64_t>(std::numeric_limits<int64_t>::max())) {
      return std::nullopt;
    }
    return static_cast<int64_t>(raw);
  }
  return std::nullopt;
}

std::optional<float> json_to_float(const nlohmann::json& value) {
  if (!value.is_number()) {
    return std::nullopt;
  }
  const double raw = value.get<double>();
  if (raw < static_cast<double>(std::numeric_limits<float>::lowest()) ||
      raw > static_cast<double>(std::numeric_limits<float>::max()) || !std::isfinite(raw)) {
    return std::nullopt;
  }
  return static_cast<float>(raw);
}

} // namespace

const char* to_string(PoiDensity density) {
  switch (density) {
  case PoiDensity::Low:
    return "low";
  case PoiDensity::Medium:
    return "medium";
  case PoiDensity::High:
    return "high";
  default:
    return "medium";
  }
}

std::optional<PoiDensity> parse_poi_density(std::string_view value) {
  const auto lowered = lowercase_string(std::string(value));
  if (lowered == "low") {
    return PoiDensity::Low;
  }
  if (lowered == "medium") {
    return PoiDensity::Medium;
  }
  if (lowered == "high") {
    return PoiDensity::High;
  }
  return std::nullopt;
}

WorldGenerationProfile sanitize_world_generation_profile(WorldGenerationProfile profile) {
  if (profile.expansion.cell_size_chunks <= 0) {
    profile.expansion.cell_size_chunks = kDefaultCellSizeChunks;
  }
  if (profile.expansion.jitter_units < 0) {
    profile.expansion.jitter_units = kDefaultJitterUnits;
  }
  const int max_jitter_units =
      std::max(0, profile.expansion.cell_size_chunks * 32 / 2 - 1);
  profile.expansion.jitter_units =
      std::clamp(profile.expansion.jitter_units, 0, max_jitter_units);
  if (profile.expansion.min_poi_spacing_units < 0) {
    profile.expansion.min_poi_spacing_units = kDefaultMinPoiSpacingUnits;
  }
  if (!std::isfinite(profile.expansion.loot_bias_multiplier) ||
      profile.expansion.loot_bias_multiplier <= 0.0F) {
    profile.expansion.loot_bias_multiplier = kDefaultLootBiasMultiplier;
  }
  if (!profile.expansion.poi_types.outpost && !profile.expansion.poi_types.ruins &&
      !profile.expansion.poi_types.loot_shrine) {
    profile.expansion.poi_types.outpost = true;
  }
  return profile;
}

nlohmann::json world_generation_profile_to_json(const WorldGenerationProfile& raw_profile) {
  const auto profile = sanitize_world_generation_profile(raw_profile);
  return {{"seed", profile.world_seed},
          {"expansion_enabled", profile.expansion.enabled},
          {"poi_density", to_string(profile.expansion.poi_density)},
          {"cell_size_chunks", profile.expansion.cell_size_chunks},
          {"jitter_units", profile.expansion.jitter_units},
          {"min_poi_spacing_units", profile.expansion.min_poi_spacing_units},
          {"poi_types",
           {{"outpost", profile.expansion.poi_types.outpost},
            {"ruins", profile.expansion.poi_types.ruins},
            {"loot_shrine", profile.expansion.poi_types.loot_shrine}}},
          {"loot_bias_multiplier", profile.expansion.loot_bias_multiplier}};
}

std::optional<WorldGenerationProfile> world_generation_profile_from_json(
    const nlohmann::json& value) {
  if (!value.is_object()) {
    return std::nullopt;
  }

  WorldGenerationProfile profile{};
  if (value.contains("seed")) {
    const auto parsed = json_to_i64(value["seed"]);
    if (!parsed.has_value() || parsed.value() < 0 ||
        parsed.value() > static_cast<int64_t>(std::numeric_limits<uint32_t>::max())) {
      return std::nullopt;
    }
    profile.world_seed = static_cast<uint32_t>(parsed.value());
  }

  if (value.contains("expansion_enabled")) {
    if (!value["expansion_enabled"].is_boolean()) {
      return std::nullopt;
    }
    profile.expansion.enabled = value["expansion_enabled"].get<bool>();
  }

  if (value.contains("poi_density")) {
    if (!value["poi_density"].is_string()) {
      return std::nullopt;
    }
    const auto parsed = parse_poi_density(value["poi_density"].get<std::string>());
    if (!parsed.has_value()) {
      return std::nullopt;
    }
    profile.expansion.poi_density = parsed.value();
  }

  if (value.contains("cell_size_chunks")) {
    const auto parsed = json_to_i64(value["cell_size_chunks"]);
    if (!parsed.has_value() || parsed.value() <= 0 ||
        parsed.value() > static_cast<int64_t>(std::numeric_limits<int>::max())) {
      return std::nullopt;
    }
    profile.expansion.cell_size_chunks = static_cast<int>(parsed.value());
  }

  if (value.contains("jitter_units")) {
    const auto parsed = json_to_i64(value["jitter_units"]);
    if (!parsed.has_value() || parsed.value() < 0 ||
        parsed.value() > static_cast<int64_t>(std::numeric_limits<int>::max())) {
      return std::nullopt;
    }
    profile.expansion.jitter_units = static_cast<int>(parsed.value());
  }

  if (value.contains("min_poi_spacing_units")) {
    const auto parsed = json_to_i64(value["min_poi_spacing_units"]);
    if (!parsed.has_value() || parsed.value() < 0 ||
        parsed.value() > static_cast<int64_t>(std::numeric_limits<int>::max())) {
      return std::nullopt;
    }
    profile.expansion.min_poi_spacing_units = static_cast<int>(parsed.value());
  }

  if (value.contains("poi_types")) {
    if (!value["poi_types"].is_object()) {
      return std::nullopt;
    }
    const auto& poi_types = value["poi_types"];
    if (poi_types.contains("outpost")) {
      if (!poi_types["outpost"].is_boolean()) {
        return std::nullopt;
      }
      profile.expansion.poi_types.outpost = poi_types["outpost"].get<bool>();
    }
    if (poi_types.contains("ruins")) {
      if (!poi_types["ruins"].is_boolean()) {
        return std::nullopt;
      }
      profile.expansion.poi_types.ruins = poi_types["ruins"].get<bool>();
    }
    if (poi_types.contains("loot_shrine")) {
      if (!poi_types["loot_shrine"].is_boolean()) {
        return std::nullopt;
      }
      profile.expansion.poi_types.loot_shrine = poi_types["loot_shrine"].get<bool>();
    }
  }

  if (value.contains("loot_bias_multiplier")) {
    const auto parsed = json_to_float(value["loot_bias_multiplier"]);
    if (!parsed.has_value() || parsed.value() <= 0.0F) {
      return std::nullopt;
    }
    profile.expansion.loot_bias_multiplier = parsed.value();
  }

  return sanitize_world_generation_profile(profile);
}

float poi_density_spawn_probability(PoiDensity density) {
  switch (density) {
  case PoiDensity::Low:
    return 0.18F;
  case PoiDensity::Medium:
    return 0.32F;
  case PoiDensity::High:
    return 0.52F;
  default:
    return 0.32F;
  }
}

} // namespace devy::voxel
