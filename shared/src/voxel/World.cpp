#include "shared/voxel/World.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <vector>

namespace devy::voxel {

namespace {

enum class PoiType : uint8_t {
  Outpost = 0,
  Ruins,
  LootShrine
};

struct PoiCandidate {
  PoiType type{PoiType::Outpost};
  int center_x{0};
  int center_z{0};
  int cell_x{0};
  int cell_z{0};
  int radius_units{0};
  uint64_t rank{0U};
  bool valid{false};
};

bool world_to_chunk_local(int world_axis, int& chunk_axis, int& local_axis) {
  if (world_axis < 0) {
    return false;
  }
  chunk_axis = world_axis / kChunkSize;
  local_axis = world_axis % kChunkSize;
  return true;
}

float hash_noise(int x, int z) {
  uint32_t n = static_cast<uint32_t>(x * 73856093) ^ static_cast<uint32_t>(z * 19349663);
  n = (n << 13) ^ n;
  const uint32_t masked = (n * (n * n * 15731u + 789221u) + 1376312589u) & 0x7fffffffU;
  return 1.0f - static_cast<float>(masked) / 1073741824.0f;
}

float smooth_noise(int x, int z) {
  const float corners =
      (hash_noise(x - 1, z - 1) + hash_noise(x + 1, z - 1) + hash_noise(x - 1, z + 1) +
       hash_noise(x + 1, z + 1)) *
      0.0625f;
  const float sides = (hash_noise(x - 1, z) + hash_noise(x + 1, z) + hash_noise(x, z - 1) +
                       hash_noise(x, z + 1)) *
                      0.125f;
  const float center = hash_noise(x, z) * 0.25f;
  return corners + sides + center;
}

float interpolated_noise(float x, float z) {
  const int int_x = static_cast<int>(std::floor(x));
  const int int_z = static_cast<int>(std::floor(z));
  const float frac_x = x - static_cast<float>(int_x);
  const float frac_z = z - static_cast<float>(int_z);

  const float v1 = smooth_noise(int_x, int_z);
  const float v2 = smooth_noise(int_x + 1, int_z);
  const float v3 = smooth_noise(int_x, int_z + 1);
  const float v4 = smooth_noise(int_x + 1, int_z + 1);

  const float i1 = v1 + (v2 - v1) * frac_x;
  const float i2 = v3 + (v4 - v3) * frac_x;

  return i1 + (i2 - i1) * frac_z;
}

float perlin(float x, float z) {
  float total = 0.0f;
  float frequency = 0.04f;
  float amplitude = 1.0f;
  constexpr float persistence = 0.5f;

  for (int octave = 0; octave < 4; ++octave) {
    total += interpolated_noise(x * frequency, z * frequency) * amplitude;
    amplitude *= persistence;
    frequency *= 2.0f;
  }
  return total;
}

int sample_height(int world_x, int world_z, int max_height) {
  if (max_height <= 0) {
    return 0;
  }
  const float height_noise = perlin(static_cast<float>(world_x), static_cast<float>(world_z));
  int height =
      static_cast<int>((height_noise + 1.0f) * 0.5f * static_cast<float>(max_height));
  height = std::clamp(height, 1, max_height);
  return height;
}

uint64_t splitmix64(uint64_t value) {
  uint64_t z = value + 0x9E3779B97F4A7C15ULL;
  z = (z ^ (z >> 30)) * 0xBF58476D1CE4E5B9ULL;
  z = (z ^ (z >> 27)) * 0x94D049BB133111EBULL;
  return z ^ (z >> 31);
}

uint64_t hash_world(uint32_t world_seed, int x, int z, uint64_t salt) {
  const uint64_t ux = static_cast<uint64_t>(static_cast<uint32_t>(x));
  const uint64_t uz = static_cast<uint64_t>(static_cast<uint32_t>(z));
  uint64_t mixed = static_cast<uint64_t>(world_seed) ^ (ux << 1U) ^ (uz << 33U) ^ salt;
  return splitmix64(mixed);
}

float hash_unit_float(uint64_t hash) {
  constexpr double inv = 1.0 / static_cast<double>(0xFFFFFFFFULL);
  const uint32_t lower = static_cast<uint32_t>(hash & 0xFFFFFFFFULL);
  return static_cast<float>(static_cast<double>(lower) * inv);
}

int hash_signed_range(uint64_t hash, int min_value, int max_value) {
  if (min_value >= max_value) {
    return min_value;
  }
  const uint64_t span = static_cast<uint64_t>(max_value - min_value + 1);
  return min_value + static_cast<int>(hash % span);
}

int floor_div(int value, int divisor) {
  if (divisor <= 0) {
    return 0;
  }
  const int quotient = value / divisor;
  const int remainder = value % divisor;
  if (remainder == 0) {
    return quotient;
  }
  if ((value < 0) != (divisor < 0)) {
    return quotient - 1;
  }
  return quotient;
}

int poi_radius_units(PoiType type) {
  switch (type) {
  case PoiType::Outpost:
    return 8;
  case PoiType::Ruins:
    return 7;
  case PoiType::LootShrine:
    return 5;
  default:
    return 5;
  }
}

bool candidate_rank_less(const PoiCandidate& lhs, const PoiCandidate& rhs) {
  if (lhs.rank != rhs.rank) {
    return lhs.rank < rhs.rank;
  }
  if (lhs.cell_x != rhs.cell_x) {
    return lhs.cell_x < rhs.cell_x;
  }
  return lhs.cell_z < rhs.cell_z;
}

bool intersects_chunk(const PoiCandidate& candidate, int chunk_x, int chunk_z) {
  const int chunk_min_x = chunk_x * kChunkSize;
  const int chunk_max_x = chunk_min_x + (kChunkSize - 1);
  const int chunk_min_z = chunk_z * kChunkSize;
  const int chunk_max_z = chunk_min_z + (kChunkSize - 1);

  const int nearest_x = std::clamp(candidate.center_x, chunk_min_x, chunk_max_x);
  const int nearest_z = std::clamp(candidate.center_z, chunk_min_z, chunk_max_z);
  const int dx = candidate.center_x - nearest_x;
  const int dz = candidate.center_z - nearest_z;
  const int radius = candidate.radius_units;
  return (dx * dx + dz * dz) <= (radius * radius);
}

PoiCandidate build_candidate(int cell_x, int cell_z,
                             const WorldGenerationProfile& generation_profile) {
  PoiCandidate candidate{};
  if (!generation_profile.expansion.enabled) {
    return candidate;
  }

  std::vector<PoiType> enabled_types{};
  enabled_types.reserve(3);
  if (generation_profile.expansion.poi_types.outpost) {
    enabled_types.push_back(PoiType::Outpost);
  }
  if (generation_profile.expansion.poi_types.ruins) {
    enabled_types.push_back(PoiType::Ruins);
  }
  if (generation_profile.expansion.poi_types.loot_shrine) {
    enabled_types.push_back(PoiType::LootShrine);
  }
  if (enabled_types.empty()) {
    return candidate;
  }

  const float spawn_probability =
      poi_density_spawn_probability(generation_profile.expansion.poi_density);
  const uint64_t chance_hash =
      hash_world(generation_profile.world_seed, cell_x, cell_z, 0xA7F4C53DULL);
  if (hash_unit_float(chance_hash) > spawn_probability) {
    return candidate;
  }

  const int cell_size_units =
      std::max(1, generation_profile.expansion.cell_size_chunks) * kChunkSize;
  const int center_base_x = cell_x * cell_size_units + cell_size_units / 2;
  const int center_base_z = cell_z * cell_size_units + cell_size_units / 2;
  const int jitter = std::max(0, generation_profile.expansion.jitter_units);

  const int offset_x = hash_signed_range(
      hash_world(generation_profile.world_seed, cell_x, cell_z, 0x1B7A0D11ULL), -jitter,
      jitter);
  const int offset_z = hash_signed_range(
      hash_world(generation_profile.world_seed, cell_x, cell_z, 0x52A8749BULL), -jitter,
      jitter);
  const uint64_t type_hash =
      hash_world(generation_profile.world_seed, cell_x, cell_z, 0x7F4A7C15ULL);

  const PoiType type = enabled_types[type_hash % enabled_types.size()];
  const int center_x = center_base_x + offset_x;
  const int center_z = center_base_z + offset_z;
  if (center_x < 0 || center_z < 0) {
    return candidate;
  }

  candidate.type = type;
  candidate.center_x = center_x;
  candidate.center_z = center_z;
  candidate.cell_x = cell_x;
  candidate.cell_z = cell_z;
  candidate.radius_units = poi_radius_units(type);
  candidate.rank = hash_world(generation_profile.world_seed, cell_x, cell_z, 0x93D76517ULL);
  candidate.valid = true;
  return candidate;
}

bool survives_spacing(const PoiCandidate& candidate,
                      const WorldGenerationProfile& generation_profile) {
  if (!candidate.valid) {
    return false;
  }
  const int spacing_units = generation_profile.expansion.min_poi_spacing_units;
  if (spacing_units <= 0) {
    return true;
  }

  const int cell_size_units =
      std::max(1, generation_profile.expansion.cell_size_chunks) * kChunkSize;
  const int cell_radius = std::max(1, spacing_units / std::max(1, cell_size_units) + 2);
  const int spacing_sq = spacing_units * spacing_units;
  for (int dz = -cell_radius; dz <= cell_radius; ++dz) {
    for (int dx = -cell_radius; dx <= cell_radius; ++dx) {
      if (dx == 0 && dz == 0) {
        continue;
      }
      const PoiCandidate other =
          build_candidate(candidate.cell_x + dx, candidate.cell_z + dz, generation_profile);
      if (!other.valid) {
        continue;
      }
      const int distance_x = other.center_x - candidate.center_x;
      const int distance_z = other.center_z - candidate.center_z;
      if (distance_x * distance_x + distance_z * distance_z >= spacing_sq) {
        continue;
      }
      if (candidate_rank_less(other, candidate)) {
        return false;
      }
    }
  }
  return true;
}

void set_block_if_in_chunk(Chunk& chunk, int chunk_x, int chunk_y, int chunk_z, int world_x,
                           int world_y, int world_z, int max_height, BlockId block_id) {
  if (world_x < 0 || world_y < 0 || world_z < 0 || world_y >= max_height) {
    return;
  }
  const int target_chunk_x = world_x / kChunkSize;
  const int target_chunk_y = world_y / kChunkSize;
  const int target_chunk_z = world_z / kChunkSize;
  if (target_chunk_x != chunk_x || target_chunk_y != chunk_y || target_chunk_z != chunk_z) {
    return;
  }

  const int local_x = world_x % kChunkSize;
  const int local_y = world_y % kChunkSize;
  const int local_z = world_z % kChunkSize;
  chunk.set(local_x, local_y, local_z, block_id);
}

void stamp_outpost(Chunk& chunk, int chunk_x, int chunk_y, int chunk_z,
                   const PoiCandidate& candidate, int base_height, int max_height) {
  const int floor_y = std::clamp(base_height - 1, 0, std::max(0, max_height - 1));
  for (int dz = -6; dz <= 6; ++dz) {
    for (int dx = -6; dx <= 6; ++dx) {
      const int world_x = candidate.center_x + dx;
      const int world_z = candidate.center_z + dz;
      set_block_if_in_chunk(chunk, chunk_x, chunk_y, chunk_z, world_x, floor_y, world_z,
                            max_height, 2U);
      const bool perimeter = std::abs(dx) == 6 || std::abs(dz) == 6;
      if (perimeter) {
        for (int h = 1; h <= 3; ++h) {
          set_block_if_in_chunk(chunk, chunk_x, chunk_y, chunk_z, world_x, floor_y + h, world_z,
                                max_height, 2U);
        }
      }
    }
  }

  for (int gate_x = -1; gate_x <= 1; ++gate_x) {
    for (int h = 1; h <= 2; ++h) {
      set_block_if_in_chunk(chunk, chunk_x, chunk_y, chunk_z, candidate.center_x + gate_x,
                            floor_y + h, candidate.center_z - 6, max_height, 0U);
    }
  }

  for (int h = 1; h <= 6; ++h) {
    set_block_if_in_chunk(chunk, chunk_x, chunk_y, chunk_z, candidate.center_x + 5, floor_y + h,
                          candidate.center_z + 5, max_height, 2U);
  }
  set_block_if_in_chunk(chunk, chunk_x, chunk_y, chunk_z, candidate.center_x + 5, floor_y + 7,
                        candidate.center_z + 5, max_height, 3U);
}

void stamp_ruins(Chunk& chunk, int chunk_x, int chunk_y, int chunk_z,
                 const PoiCandidate& candidate, int base_height, int max_height,
                 uint32_t world_seed) {
  const int floor_y = std::clamp(base_height - 1, 0, std::max(0, max_height - 1));
  for (int dz = -5; dz <= 5; ++dz) {
    for (int dx = -5; dx <= 5; ++dx) {
      const int world_x = candidate.center_x + dx;
      const int world_z = candidate.center_z + dz;
      const bool perimeter = std::abs(dx) == 5 || std::abs(dz) == 5;
      if (!perimeter) {
        const BlockId floor_block = ((dx + dz) & 1) == 0 ? 2U : 1U;
        set_block_if_in_chunk(chunk, chunk_x, chunk_y, chunk_z, world_x, floor_y, world_z,
                              max_height, floor_block);
        continue;
      }

      const uint64_t segment_hash =
          hash_world(world_seed, world_x, world_z, 0xC2B2AE3DULL);
      if ((segment_hash % 4U) == 0U) {
        continue;
      }
      for (int h = 1; h <= 2; ++h) {
        set_block_if_in_chunk(chunk, chunk_x, chunk_y, chunk_z, world_x, floor_y + h, world_z,
                              max_height, 2U);
      }
    }
  }
}

void stamp_loot_shrine(Chunk& chunk, int chunk_x, int chunk_y, int chunk_z,
                       const PoiCandidate& candidate, int base_height, int max_height) {
  const int floor_y = std::clamp(base_height - 1, 0, std::max(0, max_height - 1));
  for (int dz = -3; dz <= 3; ++dz) {
    for (int dx = -3; dx <= 3; ++dx) {
      const int world_x = candidate.center_x + dx;
      const int world_z = candidate.center_z + dz;
      const int distance_sq = dx * dx + dz * dz;
      if (distance_sq <= 10) {
        set_block_if_in_chunk(chunk, chunk_x, chunk_y, chunk_z, world_x, floor_y, world_z,
                              max_height, 2U);
      }
    }
  }

  for (int h = 1; h <= 3; ++h) {
    set_block_if_in_chunk(chunk, chunk_x, chunk_y, chunk_z, candidate.center_x, floor_y + h,
                          candidate.center_z, max_height, 2U);
  }
  set_block_if_in_chunk(chunk, chunk_x, chunk_y, chunk_z, candidate.center_x, floor_y + 4,
                        candidate.center_z, max_height, 3U);

  constexpr int kOffsets[4][2] = {{3, 0}, {-3, 0}, {0, 3}, {0, -3}};
  for (const auto& offset : kOffsets) {
    set_block_if_in_chunk(chunk, chunk_x, chunk_y, chunk_z, candidate.center_x + offset[0],
                          floor_y + 1, candidate.center_z + offset[1], max_height, 3U);
  }
}

std::vector<PoiCandidate> gather_chunk_candidates(
    int chunk_x, int chunk_z, const WorldGenerationProfile& generation_profile) {
  std::vector<PoiCandidate> out{};
  if (!generation_profile.expansion.enabled) {
    return out;
  }

  const int cell_size_units =
      std::max(1, generation_profile.expansion.cell_size_chunks) * kChunkSize;
  const int max_radius = 8;
  const int margin_units =
      max_radius + std::max(0, generation_profile.expansion.jitter_units) + cell_size_units;
  const int chunk_min_x = chunk_x * kChunkSize;
  const int chunk_max_x = chunk_min_x + (kChunkSize - 1);
  const int chunk_min_z = chunk_z * kChunkSize;
  const int chunk_max_z = chunk_min_z + (kChunkSize - 1);

  const int min_cell_x = floor_div(chunk_min_x - margin_units, cell_size_units);
  const int max_cell_x = floor_div(chunk_max_x + margin_units, cell_size_units);
  const int min_cell_z = floor_div(chunk_min_z - margin_units, cell_size_units);
  const int max_cell_z = floor_div(chunk_max_z + margin_units, cell_size_units);

  for (int cell_z = min_cell_z; cell_z <= max_cell_z; ++cell_z) {
    for (int cell_x = min_cell_x; cell_x <= max_cell_x; ++cell_x) {
      const PoiCandidate candidate = build_candidate(cell_x, cell_z, generation_profile);
      if (!candidate.valid || !survives_spacing(candidate, generation_profile) ||
          !intersects_chunk(candidate, chunk_x, chunk_z)) {
        continue;
      }
      out.push_back(candidate);
    }
  }

  std::sort(out.begin(), out.end(), [](const PoiCandidate& lhs, const PoiCandidate& rhs) {
    if (lhs.center_x != rhs.center_x) {
      return lhs.center_x < rhs.center_x;
    }
    if (lhs.center_z != rhs.center_z) {
      return lhs.center_z < rhs.center_z;
    }
    return static_cast<int>(lhs.type) < static_cast<int>(rhs.type);
  });
  return out;
}

} // namespace

World::World() = default;

void World::clear() { chunks_.clear(); }

Chunk& World::ensure_generated_chunk(int chunk_x, int chunk_y, int chunk_z, int max_height) {
  if (max_height > 0) {
    max_height_ = std::max(max_height_, max_height);
  }

  const ChunkCoord key{chunk_x, chunk_y, chunk_z};
  auto existing = chunks_.find(key);
  if (existing != chunks_.end()) {
    return existing->second;
  }

  Chunk chunk{};
  for (int z = 0; z < kChunkSize; ++z) {
    for (int x = 0; x < kChunkSize; ++x) {
      const int world_x = chunk_x * kChunkSize + x;
      const int world_z = chunk_z * kChunkSize + z;
      const int height = sample_height(world_x, world_z, max_height_);

      for (int y = 0; y < kChunkSize; ++y) {
        const int world_y = chunk_y * kChunkSize + y;
        BlockId id = 0;
        if (world_y < height - 3) {
          id = 2; // stone
        } else if (world_y < height - 1) {
          id = 1; // dirt
        } else if (world_y < height) {
          id = 3; // grass
        }
        chunk.set(x, y, z, id);
      }
    }
  }

  const auto chunk_pois = gather_chunk_candidates(chunk_x, chunk_z, generation_profile_);
  for (const auto& poi : chunk_pois) {
    const int base_height = sample_height(poi.center_x, poi.center_z, max_height_);
    switch (poi.type) {
    case PoiType::Outpost:
      stamp_outpost(chunk, chunk_x, chunk_y, chunk_z, poi, base_height, max_height_);
      break;
    case PoiType::Ruins:
      stamp_ruins(chunk, chunk_x, chunk_y, chunk_z, poi, base_height, max_height_,
                  generation_profile_.world_seed);
      break;
    case PoiType::LootShrine:
      stamp_loot_shrine(chunk, chunk_x, chunk_y, chunk_z, poi, base_height, max_height_);
      break;
    default:
      break;
    }
  }

  return chunks_.emplace(key, std::move(chunk)).first->second;
}

bool World::remove_chunk(int chunk_x, int chunk_y, int chunk_z) {
  const ChunkCoord key{chunk_x, chunk_y, chunk_z};
  return chunks_.erase(key) > 0U;
}

void World::generate(int chunks_x, int chunks_z, int max_height,
                     GenerationProgressCallback on_progress) {
  generate(chunks_x, chunks_z, max_height, generation_profile_, on_progress);
}

void World::generate(int chunks_x, int chunks_z, int max_height) {
  generate(chunks_x, chunks_z, max_height, generation_profile_, {});
}

void World::generate(int chunks_x, int chunks_z, int max_height,
                     const WorldGenerationProfile& generation_profile) {
  generate(chunks_x, chunks_z, max_height, generation_profile, {});
}

void World::generate(int chunks_x, int chunks_z, int max_height,
                     const WorldGenerationProfile& generation_profile,
                     GenerationProgressCallback on_progress) {
  clear();
  generation_profile_ = sanitize_world_generation_profile(generation_profile);
  max_height_ = max_height;

  const int chunks_y = (max_height + kChunkSize - 1) / kChunkSize;
  const std::size_t x_count = chunks_x > 0 ? static_cast<std::size_t>(chunks_x) : 0U;
  const std::size_t z_count = chunks_z > 0 ? static_cast<std::size_t>(chunks_z) : 0U;
  const std::size_t y_count = chunks_y > 0 ? static_cast<std::size_t>(chunks_y) : 0U;
  const std::size_t total_chunks = x_count * z_count * y_count;
  if (on_progress) {
    on_progress(0U, total_chunks);
  }
  if (total_chunks == 0U) {
    return;
  }

  std::size_t generated_chunks = 0U;
  for (int cy = 0; cy < chunks_y; ++cy) {
    for (int cz = 0; cz < chunks_z; ++cz) {
      for (int cx = 0; cx < chunks_x; ++cx) {
        static_cast<void>(ensure_generated_chunk(cx, cy, cz, max_height));
        ++generated_chunks;
        if (on_progress) {
          on_progress(generated_chunks, total_chunks);
        }
      }
    }
  }
}

int World::height_at(int world_x, int world_z) const {
  return sample_height(world_x, world_z, max_height_);
}

Chunk* World::get_chunk(int chunk_x, int chunk_y, int chunk_z) {
  const ChunkCoord key{chunk_x, chunk_y, chunk_z};
  const auto it = chunks_.find(key);
  if (it == chunks_.end()) {
    return nullptr;
  }
  return &it->second;
}

const Chunk* World::get_chunk(int chunk_x, int chunk_y, int chunk_z) const {
  const ChunkCoord key{chunk_x, chunk_y, chunk_z};
  const auto it = chunks_.find(key);
  if (it == chunks_.end()) {
    return nullptr;
  }
  return &it->second;
}

std::optional<BlockId> World::block_at(int world_x, int world_y, int world_z) const {
  int chunk_x = 0;
  int chunk_y = 0;
  int chunk_z = 0;
  int local_x = 0;
  int local_y = 0;
  int local_z = 0;

  if (!world_to_chunk_local(world_x, chunk_x, local_x) ||
      !world_to_chunk_local(world_y, chunk_y, local_y) ||
      !world_to_chunk_local(world_z, chunk_z, local_z)) {
    return std::nullopt;
  }

  const Chunk* chunk = get_chunk(chunk_x, chunk_y, chunk_z);
  if (!chunk) {
    return std::nullopt;
  }
  return chunk->get(local_x, local_y, local_z);
}

bool World::set_block(int world_x, int world_y, int world_z, BlockId id) {
  int chunk_x = 0;
  int chunk_y = 0;
  int chunk_z = 0;
  int local_x = 0;
  int local_y = 0;
  int local_z = 0;

  if (!world_to_chunk_local(world_x, chunk_x, local_x) ||
      !world_to_chunk_local(world_y, chunk_y, local_y) ||
      !world_to_chunk_local(world_z, chunk_z, local_z)) {
    return false;
  }

  Chunk* chunk = get_chunk(chunk_x, chunk_y, chunk_z);
  if (!chunk) {
    return false;
  }
  chunk->set(local_x, local_y, local_z, id);
  return true;
}

const std::unordered_map<ChunkCoord, Chunk, ChunkCoordHash>& World::chunks() const {
  return chunks_;
}

void World::set_generation_profile(const WorldGenerationProfile& generation_profile) {
  generation_profile_ = sanitize_world_generation_profile(generation_profile);
}

const WorldGenerationProfile& World::generation_profile() const {
  return generation_profile_;
}

} // namespace devy::voxel
