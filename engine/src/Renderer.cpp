#include "engine/Renderer.h"

#include "shared/Log.h"

#include <glad/glad.h>

#include <glm/glm.hpp>
#include <utility>

namespace devy::engine {

bool Renderer::init(const std::string& vertex_path, const std::string& fragment_path) {
  ready_ = shader_.load_from_files(vertex_path, fragment_path);
  if (!ready_) {
    devy::log::write(devy::log::Level::Error, "Renderer failed to load shaders.");
  }
  return ready_;
}

void Renderer::submit(const devy::voxel::MeshData& mesh, const glm::mat4& model,
                      const glm::vec3& aabb_min, const glm::vec3& aabb_max) {
  if (!ready_) {
    return;
  }

  Mesh gpu_mesh;
  gpu_mesh.upload(mesh);
  if (!gpu_mesh.valid()) {
    return;
  }

  glm::vec3 center = (aabb_min + aabb_max) * 0.5f;

  items_.push_back(RenderItem{std::move(gpu_mesh), model, aabb_min, aabb_max, center});
}

void Renderer::flush(const glm::mat4& view, const glm::mat4& proj,
                     const glm::vec3& camera_pos, float max_distance) {
  if (!ready_) {
    return;
  }

  glm::mat4 vp = proj * view;
  frustum_.update(vp);

  shader_.use();
  shader_.set_mat4("uView", view);
  shader_.set_mat4("uProj", proj);

  float max_distance_sq = max_distance * max_distance;

  for (const auto& item : items_) {
    glm::vec3 delta = item.center - camera_pos;
    if (glm::dot(delta, delta) > max_distance_sq) {
      continue;
    }

    if (!frustum_.intersects_aabb(item.aabb_min, item.aabb_max)) {
      continue;
    }

    shader_.set_mat4("uModel", item.model);
    item.mesh.draw();
  }
}

void Renderer::clear() {
  items_.clear();
}

} // namespace devy::engine
