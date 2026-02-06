#pragma once

#include <glm/glm.hpp>

namespace devy::engine {

class Frustum {
public:
  void update(const glm::mat4& matrix);
  bool intersects_aabb(const glm::vec3& min, const glm::vec3& max) const;

private:
  glm::vec4 planes_[6]{};
};

} // namespace devy::engine
