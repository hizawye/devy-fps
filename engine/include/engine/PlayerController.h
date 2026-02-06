#pragma once

#include <glm/glm.hpp>

#include <memory>

class btCollisionShape;
class btDiscreteDynamicsWorld;
class btRigidBody;
class btDefaultMotionState;

namespace devy::voxel {
class World;
}

namespace devy::engine {

struct PlayerInput {
  bool forward = false;
  bool back = false;
  bool left = false;
  bool right = false;
  bool sprint = false;
  bool crouch = false;
  bool jump = false;
};

class PlayerController {
public:
  PlayerController(btDiscreteDynamicsWorld* world, float radius, float height,
                   float crouch_height, float jump_height, float max_slope_deg);
  ~PlayerController();

  void set_position(const glm::vec3& position);
  void apply_input(float dt, const PlayerInput& input, const glm::vec3& camera_forward,
                   const glm::vec3& camera_right, const devy::voxel::World& world);
  void post_physics(const devy::voxel::World& world);

  glm::vec3 position() const;
  float eye_height() const;
  bool grounded() const;

private:
  void apply_crouch(bool crouch);
  float current_height() const;
  float current_half_height() const;
  void set_vertical_velocity(float value);
  void clamp_to_heightmap(const devy::voxel::World& world);

  btDiscreteDynamicsWorld* world_ = nullptr;
  btRigidBody* body_ = nullptr;
  btDefaultMotionState* motion_state_ = nullptr;

  std::unique_ptr<btCollisionShape> stand_shape_;
  std::unique_ptr<btCollisionShape> crouch_shape_;

  float radius_ = 0.35f;
  float stand_height_ = 1.8f;
  float crouch_height_ = 1.2f;
  float jump_height_ = 0.8f;
  float max_slope_rad_ = 0.785f;

  bool grounded_ = false;
  bool crouching_ = false;
  bool sliding_ = false;
  float slide_timer_ = 0.0f;
};

} // namespace devy::engine
