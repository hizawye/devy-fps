#pragma once

#include "shared/voxel/Chunk.h"

#include <cstddef>

namespace devy::engine {

class Mesh {
public:
  Mesh();
  ~Mesh();

  Mesh(const Mesh&) = delete;
  Mesh& operator=(const Mesh&) = delete;
  Mesh(Mesh&& other) noexcept;
  Mesh& operator=(Mesh&& other) noexcept;

  void upload(const devy::voxel::MeshData& mesh);
  void draw() const;
  bool valid() const;

private:
  void destroy();

  unsigned int vao_ = 0;
  unsigned int vbo_ = 0;
  unsigned int ebo_ = 0;
  int index_count_ = 0;
};

} // namespace devy::engine
