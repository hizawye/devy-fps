#pragma once

#include <cstdint>

namespace devy::game {

enum class MoveState : uint8_t {
  Idle = 0,
  Walk,
  Sprint,
  Crouch,
  Air
};

struct MovementTuning {
  float accel_ground_units_per_second2{48.0F};
  float accel_air_units_per_second2{16.0F};
  float friction_ground_units_per_second2{20.0F};
  float max_speed_walk_units_per_second{6.0F};
  float sprint_speed_multiplier{1.5F};
  float crouch_speed_multiplier{0.55F};
  float jump_velocity_units_per_second{7.5F};
  float gravity_units_per_second2{22.0F};
};

struct MovementInputIntent {
  float move_x{0.0F};
  float move_y{0.0F};
  bool jump{false};
  bool sprint{false};
  bool crouch{false};
};

struct MovementKinematicState {
  float position_x{0.0F};
  float position_y{0.0F};
  float velocity_x{0.0F};
  float velocity_y{0.0F};
  float vertical_position{0.0F};
  float vertical_velocity{0.0F};
  bool grounded{true};
  MoveState move_state{MoveState::Idle};
};

MovementTuning sanitize_movement_tuning(MovementTuning tuning);
MovementInputIntent sanitize_movement_input(MovementInputIntent input);
MovementKinematicState step_movement(const MovementKinematicState& previous,
                                     const MovementInputIntent& input, float dt_seconds,
                                     const MovementTuning& tuning);
float horizontal_speed(const MovementKinematicState& state);
const char* to_string(MoveState state);

} // namespace devy::game
