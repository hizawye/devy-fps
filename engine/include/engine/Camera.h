#pragma once

#include <glm/glm.hpp>

namespace devy::engine {

class Camera {
public:
  Camera();

  void set_position(const glm::vec3& position);
  void rotate(float yaw_delta, float pitch_delta);
  void move(const glm::vec3& direction, float delta_seconds, float speed_multiplier);

  glm::mat4 view_matrix() const;
  glm::mat4 proj_matrix(float aspect) const;

  const glm::vec3& position() const;
  glm::vec3 forward() const;
  glm::vec3 right_dir() const;

private:
  glm::vec3 front() const;
  glm::vec3 right() const;

  glm::vec3 position_;
  float yaw_;
  float pitch_;
  float speed_;
  float sensitivity_;
};

} // namespace devy::engine
