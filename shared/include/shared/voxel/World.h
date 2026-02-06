#pragma once

#include "shared/voxel/Chunk.h"

#include <unordered_map>
#include <vector>

namespace devy::voxel {

struct ChunkCoord {
  int x;
  int y;
  int z;

  bool operator==(const ChunkCoord& other) const {
    return x == other.x && y == other.y && z == other.z;
  }
};

struct ChunkCoordHash {
  std::size_t operator()(const ChunkCoord& coord) const noexcept {
    std::size_t hx = static_cast<std::size_t>(coord.x) * 73856093u;
    std::size_t hy = static_cast<std::size_t>(coord.y) * 19349663u;
    std::size_t hz = static_cast<std::size_t>(coord.z) * 83492791u;
    return hx ^ hy ^ hz;
  }
};

class World {
public:
  World();

  void generate(int chunks_x, int chunks_z, int max_height);
  int height_at(int world_x, int world_z) const;
  Chunk* get_chunk(int chunk_x, int chunk_y, int chunk_z);
  const std::unordered_map<ChunkCoord, Chunk, ChunkCoordHash>& chunks() const;

private:
  std::unordered_map<ChunkCoord, Chunk, ChunkCoordHash> chunks_;
  int max_height_ = 0;
};

} // namespace devy::voxel
