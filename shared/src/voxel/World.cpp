#include "shared/voxel/World.h"

#include <algorithm>
#include <cmath>
#include <cstdint>

namespace devy::voxel {

namespace {
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
  float corners = (hash_noise(x - 1, z - 1) + hash_noise(x + 1, z - 1) + hash_noise(x - 1, z + 1) + hash_noise(x + 1, z + 1)) * 0.0625f;
  float sides = (hash_noise(x - 1, z) + hash_noise(x + 1, z) + hash_noise(x, z - 1) + hash_noise(x, z + 1)) * 0.125f;
  float center = hash_noise(x, z) * 0.25f;
  return corners + sides + center;
}

float interpolated_noise(float x, float z) {
  int int_x = static_cast<int>(std::floor(x));
  int int_z = static_cast<int>(std::floor(z));
  float frac_x = x - static_cast<float>(int_x);
  float frac_z = z - static_cast<float>(int_z);

  float v1 = smooth_noise(int_x, int_z);
  float v2 = smooth_noise(int_x + 1, int_z);
  float v3 = smooth_noise(int_x, int_z + 1);
  float v4 = smooth_noise(int_x + 1, int_z + 1);

  float i1 = v1 + (v2 - v1) * frac_x;
  float i2 = v3 + (v4 - v3) * frac_x;

  return i1 + (i2 - i1) * frac_z;
}

float perlin(float x, float z) {
  float total = 0.0f;
  float frequency = 0.04f;
  float amplitude = 1.0f;
  float persistence = 0.5f;

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
  float height_noise = perlin(static_cast<float>(world_x), static_cast<float>(world_z));
  int height =
      static_cast<int>((height_noise + 1.0f) * 0.5f * static_cast<float>(max_height));
  height = std::clamp(height, 1, max_height);
  return height;
}
} // namespace

World::World() = default;

void World::generate(int chunks_x, int chunks_z, int max_height) {
  chunks_.clear();
  max_height_ = max_height;

  int chunks_y = (max_height + kChunkSize - 1) / kChunkSize;

  for (int cy = 0; cy < chunks_y; ++cy) {
    for (int cz = 0; cz < chunks_z; ++cz) {
      for (int cx = 0; cx < chunks_x; ++cx) {
        Chunk chunk;
        for (int z = 0; z < kChunkSize; ++z) {
          for (int x = 0; x < kChunkSize; ++x) {
            int world_x = cx * kChunkSize + x;
            int world_z = cz * kChunkSize + z;

            int height = sample_height(world_x, world_z, max_height);

            for (int y = 0; y < kChunkSize; ++y) {
              int world_y = cy * kChunkSize + y;
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
        chunks_.insert({ChunkCoord{cx, cy, cz}, chunk});
      }
    }
  }
}

int World::height_at(int world_x, int world_z) const {
  return sample_height(world_x, world_z, max_height_);
}

Chunk* World::get_chunk(int chunk_x, int chunk_y, int chunk_z) {
  ChunkCoord key{chunk_x, chunk_y, chunk_z};
  auto it = chunks_.find(key);
  if (it == chunks_.end()) {
    return nullptr;
  }
  return &it->second;
}

const Chunk* World::get_chunk(int chunk_x, int chunk_y, int chunk_z) const {
  ChunkCoord key{chunk_x, chunk_y, chunk_z};
  auto it = chunks_.find(key);
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

} // namespace devy::voxel
