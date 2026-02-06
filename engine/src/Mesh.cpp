#include "engine/Mesh.h"

#include <glad/glad.h>

#include <cstddef>

namespace devy::engine {

Mesh::Mesh() = default;

Mesh::~Mesh() {
  destroy();
}

Mesh::Mesh(Mesh&& other) noexcept {
  vao_ = other.vao_;
  vbo_ = other.vbo_;
  ebo_ = other.ebo_;
  index_count_ = other.index_count_;
  other.vao_ = 0;
  other.vbo_ = 0;
  other.ebo_ = 0;
  other.index_count_ = 0;
}

Mesh& Mesh::operator=(Mesh&& other) noexcept {
  if (this != &other) {
    destroy();
    vao_ = other.vao_;
    vbo_ = other.vbo_;
    ebo_ = other.ebo_;
    index_count_ = other.index_count_;
    other.vao_ = 0;
    other.vbo_ = 0;
    other.ebo_ = 0;
    other.index_count_ = 0;
  }
  return *this;
}

void Mesh::destroy() {
  if (ebo_) {
    glDeleteBuffers(1, &ebo_);
    ebo_ = 0;
  }
  if (vbo_) {
    glDeleteBuffers(1, &vbo_);
    vbo_ = 0;
  }
  if (vao_) {
    glDeleteVertexArrays(1, &vao_);
    vao_ = 0;
  }
  index_count_ = 0;
}

void Mesh::upload(const devy::voxel::MeshData& mesh) {
  destroy();

  if (mesh.vertices.empty() || mesh.indices.empty()) {
    return;
  }

  index_count_ = static_cast<int>(mesh.indices.size());

  glGenVertexArrays(1, &vao_);
  glGenBuffers(1, &vbo_);
  glGenBuffers(1, &ebo_);

  glBindVertexArray(vao_);

  glBindBuffer(GL_ARRAY_BUFFER, vbo_);
  glBufferData(GL_ARRAY_BUFFER,
               static_cast<GLsizeiptr>(mesh.vertices.size() * sizeof(devy::voxel::Vertex)),
               mesh.vertices.data(),
               GL_STATIC_DRAW);

  glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, ebo_);
  glBufferData(GL_ELEMENT_ARRAY_BUFFER,
               static_cast<GLsizeiptr>(mesh.indices.size() * sizeof(uint32_t)),
               mesh.indices.data(),
               GL_STATIC_DRAW);

  glEnableVertexAttribArray(0);
  glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, sizeof(devy::voxel::Vertex), reinterpret_cast<void*>(0));

  std::size_t color_offset = offsetof(devy::voxel::Vertex, r);
  glEnableVertexAttribArray(1);
  glVertexAttribPointer(1, 4, GL_UNSIGNED_BYTE, GL_TRUE, sizeof(devy::voxel::Vertex), reinterpret_cast<void*>(color_offset));

  std::size_t normal_offset = offsetof(devy::voxel::Vertex, nx);
  glEnableVertexAttribArray(2);
  glVertexAttribPointer(2, 3, GL_FLOAT, GL_FALSE, sizeof(devy::voxel::Vertex), reinterpret_cast<void*>(normal_offset));

  glBindVertexArray(0);
}

void Mesh::draw() const {
  if (vao_ == 0 || index_count_ == 0) {
    return;
  }
  glBindVertexArray(vao_);
  glDrawElements(GL_TRIANGLES, index_count_, GL_UNSIGNED_INT, nullptr);
  glBindVertexArray(0);
}

bool Mesh::valid() const {
  return vao_ != 0 && index_count_ > 0;
}

} // namespace devy::engine
