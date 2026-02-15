#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <vector>

namespace devy::voxel {

constexpr int kChunkSize = 32;
constexpr int kChunkVolume = kChunkSize * kChunkSize * kChunkSize;

using BlockId = uint8_t;

struct Vertex {
  float x;
  float y;
  float z;
  uint8_t r;
  uint8_t g;
  uint8_t b;
  uint8_t a;
  float nx;
  float ny;
  float nz;
};

struct MeshData {
  std::vector<Vertex> vertices;
  std::vector<uint32_t> indices;
};

class Chunk {
public:
  Chunk();

  BlockId get(int x, int y, int z) const;
  void set(int x, int y, int z, BlockId id);

  MeshData build_mesh() const;

private:
  std::array<BlockId, kChunkVolume> blocks_{};

  std::size_t index(int x, int y, int z) const;
  bool is_inside(int x, int y, int z) const;
};

} // namespace devy::voxel
