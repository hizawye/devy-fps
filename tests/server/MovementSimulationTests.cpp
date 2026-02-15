#include "server/MovementSimulation.h"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <chrono>
#include <vector>

namespace devy::server {
namespace {

TEST_CASE("Movement simulation normalizes movement axis and tracks input acknowledgements") {
  MovementSimulation simulation({6.0F});

  const std::vector<PlayerInputCommand> inputs{
      {1U, 7U, 3.0F, 4.0F, false, false, RuntimeTimePoint{}}};

  simulation.apply_inputs(std::chrono::milliseconds(100), inputs);

  const auto state = simulation.state_for(1U);
  REQUIRE(state.has_value());
  REQUIRE(state->last_processed_input_seq == 7U);
  REQUIRE(state->velocity_x == Catch::Approx(4.2426405F).margin(0.0001F));
  REQUIRE(state->velocity_y == Catch::Approx(4.2426405F).margin(0.0001F));
  REQUIRE(state->position_x == Catch::Approx(0.42426407F).margin(0.0001F));
  REQUIRE(state->position_y == Catch::Approx(0.42426407F).margin(0.0001F));
}

TEST_CASE("Movement simulation applies only the newest input command per player each tick") {
  MovementSimulation simulation({6.0F});

  const std::vector<PlayerInputCommand> inputs{
      {3U, 11U, 1.0F, 0.0F, false, false, RuntimeTimePoint{}},
      {3U, 12U, 0.0F, 1.0F, false, false, RuntimeTimePoint{}}};

  simulation.apply_inputs(std::chrono::milliseconds(100), inputs);

  const auto state = simulation.state_for(3U);
  REQUIRE(state.has_value());
  REQUIRE(state->last_processed_input_seq == 12U);
  REQUIRE(state->velocity_x == Catch::Approx(0.0F).margin(0.0001F));
  REQUIRE(state->velocity_y == Catch::Approx(6.0F).margin(0.0001F));
  REQUIRE(state->position_x == Catch::Approx(0.0F).margin(0.0001F));
  REQUIRE(state->position_y == Catch::Approx(0.6F).margin(0.0001F));
}

TEST_CASE("Movement simulation clears velocity on idle ticks and supports player removal") {
  MovementSimulation simulation({5.0F});
  simulation.ensure_player(1U);
  simulation.ensure_player(2U);

  simulation.apply_inputs(std::chrono::milliseconds(100),
                          {{1U, 1U, 1.0F, 0.0F, false, false, RuntimeTimePoint{}}});

  auto moving_state = simulation.state_for(1U);
  REQUIRE(moving_state.has_value());
  REQUIRE(moving_state->velocity_x == Catch::Approx(5.0F).margin(0.0001F));
  REQUIRE(moving_state->position_x == Catch::Approx(0.5F).margin(0.0001F));

  simulation.apply_inputs(std::chrono::milliseconds(100), {});
  moving_state = simulation.state_for(1U);
  REQUIRE(moving_state.has_value());
  REQUIRE(moving_state->velocity_x == Catch::Approx(0.0F).margin(0.0001F));
  REQUIRE(moving_state->position_x == Catch::Approx(0.5F).margin(0.0001F));

  simulation.remove_player(1U);
  REQUIRE_FALSE(simulation.state_for(1U).has_value());

  const auto states = simulation.snapshot();
  REQUIRE(states.size() == 1U);
  REQUIRE(states.front().player_id == 2U);
  REQUIRE(states.front().position_x == Catch::Approx(0.0F).margin(0.0001F));
}

} // namespace
} // namespace devy::server
