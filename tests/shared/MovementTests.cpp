#include "shared/game/Movement.h"

#include <catch2/catch_test_macros.hpp>

namespace devy::game {
namespace {

MovementTuning playable_tuning() {
  MovementTuning tuning{};
  tuning.accel_ground_units_per_second2 = 84.0F;
  tuning.accel_air_units_per_second2 = 28.0F;
  tuning.friction_ground_units_per_second2 = 32.0F;
  tuning.max_speed_walk_units_per_second = 6.5F;
  tuning.sprint_speed_multiplier = 1.5F;
  tuning.crouch_speed_multiplier = 0.6F;
  tuning.jump_velocity_units_per_second = 8.4F;
  tuning.gravity_units_per_second2 = 28.0F;
  return sanitize_movement_tuning(tuning);
}

TEST_CASE("Movement reaches walk speed quickly with playable tuning") {
  constexpr float kDtSeconds = 1.0F / 60.0F;
  MovementKinematicState state{};
  const MovementTuning tuning = playable_tuning();

  for (int tick = 0; tick < 6; ++tick) {
    state = step_movement(state, {1.0F, 0.0F, false, false, false}, kDtSeconds, tuning);
  }

  REQUIRE(horizontal_speed(state) >= tuning.max_speed_walk_units_per_second * 0.9F);
  REQUIRE(state.move_state == MoveState::Walk);
}

TEST_CASE("Movement brakes to near stop quickly after input release") {
  constexpr float kDtSeconds = 1.0F / 60.0F;
  MovementKinematicState state{};
  const MovementTuning tuning = playable_tuning();

  for (int tick = 0; tick < 10; ++tick) {
    state = step_movement(state, {1.0F, 0.0F, false, false, false}, kDtSeconds, tuning);
  }
  REQUIRE(horizontal_speed(state) >= tuning.max_speed_walk_units_per_second * 0.95F);

  for (int tick = 0; tick < 12; ++tick) {
    state = step_movement(state, {}, kDtSeconds, tuning);
  }
  REQUIRE(horizontal_speed(state) <= 0.35F);

  for (int tick = 0; tick < 6; ++tick) {
    state = step_movement(state, {}, kDtSeconds, tuning);
  }
  REQUIRE(horizontal_speed(state) <= 0.05F);
  REQUIRE(state.move_state == MoveState::Idle);
}

TEST_CASE("Movement reverses direction quickly with opposite input") {
  constexpr float kDtSeconds = 1.0F / 60.0F;
  MovementKinematicState state{};
  const MovementTuning tuning = playable_tuning();

  for (int tick = 0; tick < 10; ++tick) {
    state = step_movement(state, {1.0F, 0.0F, false, false, false}, kDtSeconds, tuning);
  }
  REQUIRE(state.velocity_x > 0.0F);

  for (int tick = 0; tick < 7; ++tick) {
    state = step_movement(state, {-1.0F, 0.0F, false, false, false}, kDtSeconds, tuning);
  }
  REQUIRE(state.velocity_x < 0.0F);
}

} // namespace
} // namespace devy::game
