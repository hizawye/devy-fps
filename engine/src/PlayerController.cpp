#include "engine/PlayerController.h"

#include "shared/voxel/World.h"

#include <glm/gtc/matrix_transform.hpp>

#include <btBulletDynamicsCommon.h>

#include <algorithm>
#include <cmath>

namespace devy::engine {

namespace {
constexpr float kWalkSpeed = 6.0f;
constexpr float kSprintSpeed = 10.0f;
constexpr float kCrouchSpeed = 3.5f;
constexpr float kSlideSpeed = 12.0f;
constexpr float kSlideDuration = 0.6f;
constexpr float kGravity = 9.8f;
}

PlayerController::PlayerController(btDiscreteDynamicsWorld* world, float radius, float height,
                                   float crouch_height, float jump_height, float max_slope_deg)
  : world_(world),
    radius_(radius),
    stand_height_(height),
    crouch_height_(crouch_height),
    jump_height_(jump_height),
    max_slope_rad_(glm::radians(max_slope_deg)) {
  float stand_cyl = std::max(0.1f, stand_height_ - 2.0f * radius_);
  float crouch_cyl = std::max(0.1f, crouch_height_ - 2.0f * radius_);

  stand_shape_ = std::make_unique<btCapsuleShape>(radius_, stand_cyl);
  crouch_shape_ = std::make_unique<btCapsuleShape>(radius_, crouch_cyl);

  btTransform start;
  start.setIdentity();
  start.setOrigin(btVector3(0.0f, 10.0f, 0.0f));

  btScalar mass = 80.0f;
  btVector3 inertia(0, 0, 0);
  stand_shape_->calculateLocalInertia(mass, inertia);

  motion_state_ = new btDefaultMotionState(start);
  btRigidBody::btRigidBodyConstructionInfo info(mass, motion_state_, stand_shape_.get(), inertia);
  body_ = new btRigidBody(info);
  body_->setAngularFactor(btVector3(0, 0, 0));
  body_->setFriction(0.8f);
  body_->setRestitution(0.0f);

  if (world_) {
    world_->addRigidBody(body_);
  }
}

PlayerController::~PlayerController() {
  if (world_ && body_) {
    world_->removeRigidBody(body_);
  }
  delete body_;
  delete motion_state_;
}

void PlayerController::set_position(const glm::vec3& position) {
  if (!body_) {
    return;
  }
  btTransform transform;
  transform.setIdentity();
  transform.setOrigin(btVector3(position.x, position.y, position.z));
  body_->setWorldTransform(transform);
  if (body_->getMotionState()) {
    body_->getMotionState()->setWorldTransform(transform);
  }
}

glm::vec3 PlayerController::position() const {
  if (!body_) {
    return glm::vec3(0.0f);
  }
  btTransform transform;
  if (body_->getMotionState()) {
    body_->getMotionState()->getWorldTransform(transform);
  } else {
    transform = body_->getWorldTransform();
  }
  auto origin = transform.getOrigin();
  return glm::vec3(origin.x(), origin.y(), origin.z());
}

float PlayerController::eye_height() const {
  return current_height() * 0.9f;
}

bool PlayerController::grounded() const {
  return grounded_;
}

void PlayerController::apply_crouch(bool crouch) {
  if (crouch == crouching_ || !body_) {
    return;
  }

  float feet_y = position().y - current_half_height();
  crouching_ = crouch;

  btCollisionShape* shape = crouching_ ? crouch_shape_.get() : stand_shape_.get();
  body_->setCollisionShape(shape);

  float new_half = current_half_height();
  glm::vec3 pos = position();
  pos.y = feet_y + new_half;
  set_position(pos);

  if (world_) {
    world_->updateSingleAabb(body_);
  }
}

float PlayerController::current_height() const {
  return crouching_ ? crouch_height_ : stand_height_;
}

float PlayerController::current_half_height() const {
  return current_height() * 0.5f;
}

void PlayerController::set_vertical_velocity(float value) {
  if (!body_) {
    return;
  }
  btVector3 velocity = body_->getLinearVelocity();
  velocity.setY(value);
  body_->setLinearVelocity(velocity);
}

void PlayerController::apply_input(float dt, const PlayerInput& input, const glm::vec3& camera_forward,
                                   const glm::vec3& camera_right, const devy::voxel::World& world) {
  if (!body_) {
    return;
  }

  if (sliding_) {
    slide_timer_ -= dt;
    if (slide_timer_ <= 0.0f) {
      sliding_ = false;
      slide_timer_ = 0.0f;
      apply_crouch(input.crouch);
    }
  } else if (input.crouch && input.sprint && grounded_) {
    sliding_ = true;
    slide_timer_ = kSlideDuration;
    apply_crouch(true);
  } else {
    apply_crouch(input.crouch);
  }

  glm::vec3 forward = glm::normalize(glm::vec3(camera_forward.x, 0.0f, camera_forward.z));
  glm::vec3 right = glm::normalize(glm::vec3(camera_right.x, 0.0f, camera_right.z));

  glm::vec3 move_dir(0.0f);
  if (!sliding_) {
    if (input.forward) {
      move_dir += forward;
    }
    if (input.back) {
      move_dir -= forward;
    }
    if (input.right) {
      move_dir += right;
    }
    if (input.left) {
      move_dir -= right;
    }
  } else {
    move_dir = forward;
  }

  float speed = kWalkSpeed;
  if (sliding_) {
    speed = kSlideSpeed;
  } else if (crouching_) {
    speed = kCrouchSpeed;
  } else if (input.sprint) {
    speed = kSprintSpeed;
  }

  if (glm::length(move_dir) > 0.0f) {
    move_dir = glm::normalize(move_dir);
  }

  glm::vec3 pos = position();
  float current_ground = static_cast<float>(world.height_at(static_cast<int>(pos.x), static_cast<int>(pos.z)));
  float dx = move_dir.x * speed * dt;
  float dz = move_dir.z * speed * dt;
  float run = std::sqrt(dx * dx + dz * dz);
  if (run > 0.0f) {
    float next_ground = static_cast<float>(world.height_at(static_cast<int>(pos.x + dx), static_cast<int>(pos.z + dz)));
    float rise = next_ground - current_ground;
    float max_rise = std::tan(max_slope_rad_) * run;
    if (rise > max_rise) {
      dx = 0.0f;
      dz = 0.0f;
      move_dir = glm::vec3(0.0f);
    }
  }

  btVector3 velocity = body_->getLinearVelocity();
  velocity.setX(move_dir.x * speed);
  velocity.setZ(move_dir.z * speed);
  body_->setLinearVelocity(velocity);

  if (input.jump && grounded_) {
    float jump_velocity = std::sqrt(2.0f * kGravity * jump_height_);
    set_vertical_velocity(jump_velocity);
    grounded_ = false;
  }
}

void PlayerController::post_physics(const devy::voxel::World& world) {
  clamp_to_heightmap(world);
}

void PlayerController::clamp_to_heightmap(const devy::voxel::World& world) {
  glm::vec3 pos = position();
  float ground = static_cast<float>(world.height_at(static_cast<int>(pos.x), static_cast<int>(pos.z)));
  float feet = pos.y - current_half_height();

  float epsilon = 0.05f;
  if (feet < ground + epsilon) {
    pos.y = ground + current_half_height();
    set_position(pos);
    set_vertical_velocity(0.0f);
    grounded_ = true;
  } else {
    grounded_ = false;
  }
}

} // namespace devy::engine
