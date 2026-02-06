#include "shared/voxel/Chunk.h"

#include <array>

namespace devy::voxel {

namespace {
struct Color {
  uint8_t r;
  uint8_t g;
  uint8_t b;
  uint8_t a;
};

Color block_color(BlockId id) {
  switch (id) {
    case 1: return Color{0x7A, 0x5C, 0x3A, 0xFF}; // dirt
    case 2: return Color{0x88, 0x88, 0x88, 0xFF}; // stone
    case 3: return Color{0x2E, 0x8B, 0x57, 0xFF}; // grass
    default: return Color{0x00, 0x00, 0x00, 0x00};
  }
}

struct Face {
  std::array<std::array<int, 3>, 4> corners;
  std::array<float, 3> normal;
};

const std::array<Face, 6> kFaces = {
  Face{{{{1,0,0}}, {{1,1,0}}, {{1,1,1}}, {{1,0,1}}}, {{1.0f, 0.0f, 0.0f}}}, // +X
  Face{{{{0,0,1}}, {{0,1,1}}, {{0,1,0}}, {{0,0,0}}}, {{-1.0f, 0.0f, 0.0f}}}, // -X
  Face{{{{0,1,1}}, {{1,1,1}}, {{1,1,0}}, {{0,1,0}}}, {{0.0f, 1.0f, 0.0f}}}, // +Y
  Face{{{{0,0,0}}, {{1,0,0}}, {{1,0,1}}, {{0,0,1}}}, {{0.0f, -1.0f, 0.0f}}}, // -Y
  Face{{{{0,0,1}}, {{1,0,1}}, {{1,1,1}}, {{0,1,1}}}, {{0.0f, 0.0f, 1.0f}}}, // +Z
  Face{{{{1,0,0}}, {{0,0,0}}, {{0,1,0}}, {{1,1,0}}}, {{0.0f, 0.0f, -1.0f}}}  // -Z
};

const std::array<std::array<int, 3>, 6> kNeighborOffsets = {
  std::array<int,3>{1,0,0},
  std::array<int,3>{-1,0,0},
  std::array<int,3>{0,1,0},
  std::array<int,3>{0,-1,0},
  std::array<int,3>{0,0,1},
  std::array<int,3>{0,0,-1}
};
} // namespace

Chunk::Chunk() {
  blocks_.fill(0);
}

int Chunk::index(int x, int y, int z) const {
  return x + (y * kChunkSize) + (z * kChunkSize * kChunkSize);
}

bool Chunk::is_inside(int x, int y, int z) const {
  return x >= 0 && x < kChunkSize && y >= 0 && y < kChunkSize && z >= 0 && z < kChunkSize;
}

BlockId Chunk::get(int x, int y, int z) const {
  if (!is_inside(x, y, z)) {
    return 0;
  }
  return blocks_[index(x, y, z)];
}

void Chunk::set(int x, int y, int z, BlockId id) {
  if (!is_inside(x, y, z)) {
    return;
  }
  blocks_[index(x, y, z)] = id;
}

MeshData Chunk::build_mesh() const {
  MeshData mesh;
  mesh.vertices.reserve(4096);
  mesh.indices.reserve(6144);

  for (int z = 0; z < kChunkSize; ++z) {
    for (int y = 0; y < kChunkSize; ++y) {
      for (int x = 0; x < kChunkSize; ++x) {
        BlockId id = get(x, y, z);
        if (id == 0) {
          continue;
        }

        Color color = block_color(id);

        for (size_t face = 0; face < kFaces.size(); ++face) {
          const auto& offset = kNeighborOffsets[face];
          BlockId neighbor = get(x + offset[0], y + offset[1], z + offset[2]);
          if (neighbor != 0) {
            continue;
          }

          uint32_t base_index = static_cast<uint32_t>(mesh.vertices.size());
          for (const auto& corner : kFaces[face].corners) {
            mesh.vertices.push_back(Vertex{
              static_cast<float>(x + corner[0]),
              static_cast<float>(y + corner[1]),
              static_cast<float>(z + corner[2]),
              color.r,
              color.g,
              color.b,
              color.a,
              kFaces[face].normal[0],
              kFaces[face].normal[1],
              kFaces[face].normal[2]
            });
          }

          mesh.indices.push_back(base_index + 0);
          mesh.indices.push_back(base_index + 1);
          mesh.indices.push_back(base_index + 2);
          mesh.indices.push_back(base_index + 2);
          mesh.indices.push_back(base_index + 3);
          mesh.indices.push_back(base_index + 0);
        }
      }
    }
  }

  return mesh;
}

} // namespace devy::voxel
