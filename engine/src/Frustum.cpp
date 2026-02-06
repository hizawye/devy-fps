#include "engine/Frustum.h"

#include <glm/gtc/matrix_access.hpp>

namespace devy::engine {

namespace {
void normalize_plane(glm::vec4& plane) {
  float length = glm::length(glm::vec3(plane));
  if (length > 0.0f) {
    plane /= length;
  }
}
}

void Frustum::update(const glm::mat4& matrix) {
  glm::vec4 row0 = glm::row(matrix, 0);
  glm::vec4 row1 = glm::row(matrix, 1);
  glm::vec4 row2 = glm::row(matrix, 2);
  glm::vec4 row3 = glm::row(matrix, 3);

  planes_[0] = row3 + row0; // left
  planes_[1] = row3 - row0; // right
  planes_[2] = row3 + row1; // bottom
  planes_[3] = row3 - row1; // top
  planes_[4] = row3 + row2; // near
  planes_[5] = row3 - row2; // far

  for (auto& plane : planes_) {
    normalize_plane(plane);
  }
}

bool Frustum::intersects_aabb(const glm::vec3& min, const glm::vec3& max) const {
  for (const auto& plane : planes_) {
    glm::vec3 positive = min;
    if (plane.x >= 0.0f) {
      positive.x = max.x;
    }
    if (plane.y >= 0.0f) {
      positive.y = max.y;
    }
    if (plane.z >= 0.0f) {
      positive.z = max.z;
    }

    float distance = plane.x * positive.x + plane.y * positive.y + plane.z * positive.z + plane.w;
    if (distance < 0.0f) {
      return false;
    }
  }

  return true;
}

} // namespace devy::engine
