#pragma once

#include "shared/voxel/Chunk.h"

#include <optional>
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

  void clear();
  void generate(int chunks_x, int chunks_z, int max_height);
  Chunk& ensure_generated_chunk(int chunk_x, int chunk_y, int chunk_z, int max_height);
  bool remove_chunk(int chunk_x, int chunk_y, int chunk_z);
  int height_at(int world_x, int world_z) const;
  Chunk* get_chunk(int chunk_x, int chunk_y, int chunk_z);
  const Chunk* get_chunk(int chunk_x, int chunk_y, int chunk_z) const;
  std::optional<BlockId> block_at(int world_x, int world_y, int world_z) const;
  bool set_block(int world_x, int world_y, int world_z, BlockId id);
  const std::unordered_map<ChunkCoord, Chunk, ChunkCoordHash>& chunks() const;

private:
  std::unordered_map<ChunkCoord, Chunk, ChunkCoordHash> chunks_;
  int max_height_ = 0;
};

} // namespace devy::voxel
