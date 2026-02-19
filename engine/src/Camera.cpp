#include "engine/Camera.h"

#include <glm/gtc/matrix_transform.hpp>

#include <algorithm>
#include <cmath>

namespace devy::engine {

Camera::Camera()
  : position_(0.0f, 20.0f, 40.0f),
    yaw_(-90.0f),
    pitch_(-20.0f),
    speed_(12.0f),
    sensitivity_(0.1f),
    fov_degrees_(70.0f) {
}

void Camera::set_position(const glm::vec3& position) {
  position_ = position;
}

void Camera::set_sensitivity(float sensitivity) {
  if (std::isfinite(sensitivity) && sensitivity > 0.0f) {
    sensitivity_ = sensitivity;
  }
}

void Camera::set_fov_degrees(float fov_degrees) {
  if (!std::isfinite(fov_degrees)) {
    return;
  }
  fov_degrees_ = std::clamp(fov_degrees, 45.0f, 120.0f);
}

void Camera::rotate(float yaw_delta, float pitch_delta) {
  yaw_ += yaw_delta * sensitivity_;
  pitch_ += pitch_delta * sensitivity_;
  pitch_ = std::clamp(pitch_, -89.0f, 89.0f);
}

void Camera::move(const glm::vec3& direction, float delta_seconds, float speed_multiplier) {
  glm::vec3 forward = front();
  glm::vec3 right_dir = right();
  glm::vec3 up_dir = glm::vec3(0.0f, 1.0f, 0.0f);

  glm::vec3 velocity = (right_dir * direction.x) + (up_dir * direction.y) + (forward * direction.z);
  if (glm::length(velocity) > 0.0f) {
    velocity = glm::normalize(velocity);
  }

  float speed = speed_ * speed_multiplier;
  position_ += velocity * speed * delta_seconds;
}

glm::mat4 Camera::view_matrix() const {
  return glm::lookAt(position_, position_ + front(), glm::vec3(0.0f, 1.0f, 0.0f));
}

glm::mat4 Camera::proj_matrix(float aspect) const {
  return glm::perspective(glm::radians(fov_degrees_), aspect, 0.1f, 1000.0f);
}

const glm::vec3& Camera::position() const {
  return position_;
}

glm::vec3 Camera::forward() const {
  return front();
}

glm::vec3 Camera::right_dir() const {
  return right();
}

glm::vec3 Camera::front() const {
  float yaw_rad = glm::radians(yaw_);
  float pitch_rad = glm::radians(pitch_);
  glm::vec3 f;
  f.x = std::cos(yaw_rad) * std::cos(pitch_rad);
  f.y = std::sin(pitch_rad);
  f.z = std::sin(yaw_rad) * std::cos(pitch_rad);
  return glm::normalize(f);
}

glm::vec3 Camera::right() const {
  return glm::normalize(glm::cross(front(), glm::vec3(0.0f, 1.0f, 0.0f)));
}

} // namespace devy::engine
