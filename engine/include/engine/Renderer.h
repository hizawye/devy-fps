#pragma once

#include "engine/Frustum.h"
#include "engine/Mesh.h"
#include "engine/Shader.h"
#include "shared/voxel/Chunk.h"

#include <glm/glm.hpp>

#include <string>
#include <vector>

namespace devy::engine {

class Renderer {
public:
  bool init(const std::string& vertex_path, const std::string& fragment_path);
  void submit(const devy::voxel::MeshData& mesh, const glm::mat4& model,
              const glm::vec3& aabb_min, const glm::vec3& aabb_max);
  void flush(const glm::mat4& view, const glm::mat4& proj,
             const glm::vec3& camera_pos, float max_distance);
  void clear();

private:
  struct RenderItem {
    Mesh mesh;
    glm::mat4 model;
    glm::vec3 aabb_min;
    glm::vec3 aabb_max;
    glm::vec3 center;
  };

  Shader shader_;
  Frustum frustum_;
  std::vector<RenderItem> items_;
  bool ready_ = false;
};

} // namespace devy::engine
