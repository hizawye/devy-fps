#include "server/MovementSimulation.h"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <chrono>
#include <vector>

namespace devy::server {
namespace {

PlayerInputCommand make_input(uint32_t player_id, uint32_t input_seq, float move_x, float move_y,
                              bool jump = false, bool sprint = false, bool crouch = false) {
  PlayerInputCommand input{};
  input.player_id = player_id;
  input.input_seq = input_seq;
  input.move_x = move_x;
  input.move_y = move_y;
  input.jump = jump;
  input.fire = false;
  input.received_at = RuntimeTimePoint{};
  input.sprint = sprint;
  input.crouch = crouch;
  return input;
}

game::MovementTuning default_tuning_with_speed(float max_speed) {
  game::MovementTuning tuning{};
  tuning.max_speed_walk_units_per_second = max_speed;
  return game::sanitize_movement_tuning(tuning);
}

TEST_CASE("Movement simulation normalizes movement axis and tracks input acknowledgements") {
  MovementSimulation simulation(MovementConfig{6.0F});

  const std::vector<PlayerInputCommand> inputs{make_input(1U, 7U, 3.0F, 4.0F)};

  simulation.apply_inputs(std::chrono::milliseconds(100), inputs);

  const auto state = simulation.state_for(1U);
  REQUIRE(state.has_value());
  const auto expected = game::step_movement({}, {3.0F, 4.0F, false, false, false}, 0.1F,
                                            default_tuning_with_speed(6.0F));
  REQUIRE(state->last_processed_input_seq == 7U);
  REQUIRE(state->velocity_x == Catch::Approx(expected.velocity_x).margin(0.0001F));
  REQUIRE(state->velocity_y == Catch::Approx(expected.velocity_y).margin(0.0001F));
  REQUIRE(state->position_x == Catch::Approx(expected.position_x).margin(0.0001F));
  REQUIRE(state->position_y == Catch::Approx(expected.position_y).margin(0.0001F));
  REQUIRE(state->speed == Catch::Approx(game::horizontal_speed(expected)).margin(0.0001F));
  REQUIRE(state->grounded == expected.grounded);
  REQUIRE(state->move_state == expected.move_state);
}

TEST_CASE("Movement simulation applies only the newest input command per player each tick") {
  MovementSimulation simulation(MovementConfig{6.0F});

  const std::vector<PlayerInputCommand> inputs{
      make_input(3U, 11U, 1.0F, 0.0F),
      make_input(3U, 12U, 0.0F, 1.0F)};

  simulation.apply_inputs(std::chrono::milliseconds(100), inputs);

  const auto state = simulation.state_for(3U);
  REQUIRE(state.has_value());
  const auto expected =
      game::step_movement({}, {0.0F, 1.0F, false, false, false}, 0.1F, default_tuning_with_speed(6.0F));
  REQUIRE(state->last_processed_input_seq == 12U);
  REQUIRE(state->velocity_x == Catch::Approx(expected.velocity_x).margin(0.0001F));
  REQUIRE(state->velocity_y == Catch::Approx(expected.velocity_y).margin(0.0001F));
  REQUIRE(state->position_x == Catch::Approx(expected.position_x).margin(0.0001F));
  REQUIRE(state->position_y == Catch::Approx(expected.position_y).margin(0.0001F));
}

TEST_CASE("Movement simulation clears velocity on idle ticks and supports player removal") {
  MovementSimulation simulation(MovementConfig{5.0F});
  simulation.ensure_player(1U);
  simulation.ensure_player(2U);

  simulation.apply_inputs(std::chrono::milliseconds(100), {make_input(1U, 1U, 1.0F, 0.0F)});

  auto moving_state = simulation.state_for(1U);
  REQUIRE(moving_state.has_value());
  const auto first_step =
      game::step_movement({}, {1.0F, 0.0F, false, false, false}, 0.1F, default_tuning_with_speed(5.0F));
  REQUIRE(moving_state->velocity_x == Catch::Approx(first_step.velocity_x).margin(0.0001F));
  REQUIRE(moving_state->position_x == Catch::Approx(first_step.position_x).margin(0.0001F));

  simulation.apply_inputs(std::chrono::milliseconds(100), {});
  moving_state = simulation.state_for(1U);
  REQUIRE(moving_state.has_value());
  const auto idle_step = game::step_movement(first_step, {}, 0.1F, default_tuning_with_speed(5.0F));
  REQUIRE(moving_state->velocity_x == Catch::Approx(idle_step.velocity_x).margin(0.0001F));
  REQUIRE(moving_state->position_x == Catch::Approx(idle_step.position_x).margin(0.0001F));

  simulation.remove_player(1U);
  REQUIRE_FALSE(simulation.state_for(1U).has_value());

  const auto states = simulation.snapshot();
  REQUIRE(states.size() == 1U);
  REQUIRE(states.front().player_id == 2U);
  REQUIRE(states.front().position_x == Catch::Approx(0.0F).margin(0.0001F));
}

TEST_CASE("Movement simulation tracks sprint crouch and jump state transitions") {
  MovementSimulation simulation(MovementConfig{6.0F});
  simulation.ensure_player(9U);

  simulation.apply_inputs(std::chrono::milliseconds(100), {make_input(9U, 1U, 1.0F, 0.0F, false, true)});
  const auto sprint_state = simulation.state_for(9U);
  REQUIRE(sprint_state.has_value());
  REQUIRE(sprint_state->move_state == game::MoveState::Sprint);
  REQUIRE(sprint_state->speed > 0.0F);

  simulation.apply_inputs(std::chrono::milliseconds(100),
                          {make_input(9U, 2U, 1.0F, 0.0F, false, false, true)});
  const auto crouch_state = simulation.state_for(9U);
  REQUIRE(crouch_state.has_value());
  REQUIRE(crouch_state->move_state == game::MoveState::Crouch);
  REQUIRE(crouch_state->grounded);

  simulation.apply_inputs(std::chrono::milliseconds(100), {make_input(9U, 3U, 0.0F, 0.0F, true)});
  const auto air_state = simulation.state_for(9U);
  REQUIRE(air_state.has_value());
  REQUIRE_FALSE(air_state->grounded);
  REQUIRE(air_state->move_state == game::MoveState::Air);
  REQUIRE(air_state->vertical_position > 0.0F);
}

} // namespace
} // namespace devy::server
