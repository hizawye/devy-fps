#include "shared/game/Movement.h"

#include <algorithm>
#include <cmath>
#include <limits>

namespace devy::game {
namespace {

constexpr float kInputMagnitudeEpsilon = 0.000001F;
constexpr float kSpeedEpsilon = 0.0001F;

float sanitize_positive(float value, float fallback) {
  if (!std::isfinite(value) || value <= 0.0F) {
    return fallback;
  }
  return value;
}

float sanitize_non_negative(float value, float fallback) {
  if (!std::isfinite(value) || value < 0.0F) {
    return fallback;
  }
  return value;
}

float sanitize_axis(float axis) {
  if (!std::isfinite(axis)) {
    return 0.0F;
  }
  return std::clamp(axis, -1.0F, 1.0F);
}

void normalize_or_zero(float* x, float* y) {
  const double x64 = static_cast<double>(*x);
  const double y64 = static_cast<double>(*y);
  const double mag_sq = (x64 * x64) + (y64 * y64);
  if (mag_sq <= static_cast<double>(kInputMagnitudeEpsilon)) {
    *x = 0.0F;
    *y = 0.0F;
    return;
  }

  const float mag = static_cast<float>(std::sqrt(mag_sq));
  if (mag > 1.0F) {
    *x /= mag;
    *y /= mag;
  }
}

} // namespace

MovementTuning sanitize_movement_tuning(MovementTuning tuning) {
  tuning.accel_ground_units_per_second2 =
      sanitize_positive(tuning.accel_ground_units_per_second2, 48.0F);
  tuning.accel_air_units_per_second2 = sanitize_positive(tuning.accel_air_units_per_second2, 16.0F);
  tuning.friction_ground_units_per_second2 =
      sanitize_non_negative(tuning.friction_ground_units_per_second2, 20.0F);
  tuning.max_speed_walk_units_per_second =
      sanitize_positive(tuning.max_speed_walk_units_per_second, 6.0F);
  tuning.sprint_speed_multiplier = sanitize_positive(tuning.sprint_speed_multiplier, 1.5F);
  tuning.crouch_speed_multiplier = sanitize_positive(tuning.crouch_speed_multiplier, 0.55F);
  tuning.jump_velocity_units_per_second = sanitize_positive(tuning.jump_velocity_units_per_second, 7.5F);
  tuning.gravity_units_per_second2 = sanitize_positive(tuning.gravity_units_per_second2, 22.0F);
  return tuning;
}

MovementInputIntent sanitize_movement_input(MovementInputIntent input) {
  input.move_x = sanitize_axis(input.move_x);
  input.move_y = sanitize_axis(input.move_y);
  normalize_or_zero(&input.move_x, &input.move_y);
  return input;
}

float horizontal_speed(const MovementKinematicState& state) {
  const double x64 = static_cast<double>(state.velocity_x);
  const double y64 = static_cast<double>(state.velocity_y);
  return static_cast<float>(std::sqrt((x64 * x64) + (y64 * y64)));
}

MovementKinematicState step_movement(const MovementKinematicState& previous,
                                     const MovementInputIntent& raw_input, float dt_seconds,
                                     const MovementTuning& raw_tuning) {
  MovementKinematicState next = previous;
  const MovementInputIntent input = sanitize_movement_input(raw_input);
  const MovementTuning tuning = sanitize_movement_tuning(raw_tuning);

  if (!std::isfinite(dt_seconds) || dt_seconds <= 0.0F) {
    const float speed = horizontal_speed(next);
    if (!next.grounded) {
      next.move_state = MoveState::Air;
    } else if (speed <= kSpeedEpsilon) {
      next.move_state = MoveState::Idle;
    } else if (input.crouch) {
      next.move_state = MoveState::Crouch;
    } else if (input.sprint) {
      next.move_state = MoveState::Sprint;
    } else {
      next.move_state = MoveState::Walk;
    }
    return next;
  }

  const float speed_multiplier = input.crouch
                                     ? tuning.crouch_speed_multiplier
                                     : (input.sprint ? tuning.sprint_speed_multiplier : 1.0F);
  const float target_speed = tuning.max_speed_walk_units_per_second * speed_multiplier;
  const float desired_velocity_x = input.move_x * target_speed;
  const float desired_velocity_y = input.move_y * target_speed;

  const float accel =
      next.grounded ? tuning.accel_ground_units_per_second2 : tuning.accel_air_units_per_second2;
  const float max_velocity_delta = accel * dt_seconds;
  const float delta_x = desired_velocity_x - next.velocity_x;
  const float delta_y = desired_velocity_y - next.velocity_y;
  const float delta_mag = std::sqrt((delta_x * delta_x) + (delta_y * delta_y));
  if (delta_mag > std::numeric_limits<float>::epsilon()) {
    const float scale = std::min(1.0F, max_velocity_delta / delta_mag);
    next.velocity_x += delta_x * scale;
    next.velocity_y += delta_y * scale;
  }

  const bool has_movement_input =
      std::fabs(input.move_x) > kInputMagnitudeEpsilon || std::fabs(input.move_y) > kInputMagnitudeEpsilon;
  if (!has_movement_input && next.grounded && tuning.friction_ground_units_per_second2 > 0.0F) {
    const float speed = horizontal_speed(next);
    if (speed > kSpeedEpsilon) {
      const float reduced = std::max(0.0F, speed - (tuning.friction_ground_units_per_second2 * dt_seconds));
      const float scale = (speed > std::numeric_limits<float>::epsilon()) ? (reduced / speed) : 0.0F;
      next.velocity_x *= scale;
      next.velocity_y *= scale;
    } else {
      next.velocity_x = 0.0F;
      next.velocity_y = 0.0F;
    }
  }

  if (input.jump && next.grounded) {
    next.vertical_velocity = tuning.jump_velocity_units_per_second;
    next.grounded = false;
  }

  if (!next.grounded) {
    next.vertical_velocity -= tuning.gravity_units_per_second2 * dt_seconds;
    next.vertical_position += next.vertical_velocity * dt_seconds;
    if (next.vertical_position <= 0.0F) {
      next.vertical_position = 0.0F;
      next.vertical_velocity = 0.0F;
      next.grounded = true;
    }
  } else {
    next.vertical_position = 0.0F;
    if (next.vertical_velocity < 0.0F) {
      next.vertical_velocity = 0.0F;
    }
  }

  next.position_x += next.velocity_x * dt_seconds;
  next.position_y += next.velocity_y * dt_seconds;

  const float final_speed = horizontal_speed(next);
  if (!next.grounded) {
    next.move_state = MoveState::Air;
  } else if (final_speed <= kSpeedEpsilon) {
    next.move_state = MoveState::Idle;
  } else if (input.crouch) {
    next.move_state = MoveState::Crouch;
  } else if (input.sprint) {
    next.move_state = MoveState::Sprint;
  } else {
    next.move_state = MoveState::Walk;
  }

  return next;
}

const char* to_string(MoveState state) {
  switch (state) {
    case MoveState::Idle: return "idle";
    case MoveState::Walk: return "walk";
    case MoveState::Sprint: return "sprint";
    case MoveState::Crouch: return "crouch";
    case MoveState::Air: return "air";
    default: return "unknown";
  }
}

} // namespace devy::game
