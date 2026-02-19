#include "server/MovementSimulation.h"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <utility>

namespace devy::server {
namespace {

} // namespace

MovementSimulation::MovementSimulation(MovementConfig config) : config_(sanitize_config(config)) {}

void MovementSimulation::reset() { states_by_player_.clear(); }

void MovementSimulation::ensure_player(uint32_t player_id) {
  auto [it, inserted] = states_by_player_.try_emplace(player_id);
  if (inserted) {
    it->second.player_id = player_id;
  }
}

void MovementSimulation::remove_player(uint32_t player_id) { states_by_player_.erase(player_id); }

void MovementSimulation::apply_inputs(std::chrono::nanoseconds tick_interval,
                                      const std::vector<PlayerInputCommand>& inputs) {
  if (tick_interval <= std::chrono::nanoseconds::zero()) {
    return;
  }

  const double dt_seconds = static_cast<double>(tick_interval.count()) / 1'000'000'000.0;
  const float dt = static_cast<float>(dt_seconds);
  if (dt <= 0.0F) {
    return;
  }

  std::unordered_map<uint32_t, const PlayerInputCommand*> latest_input_by_player{};
  latest_input_by_player.reserve(inputs.size());
  for (const auto& input : inputs) {
    latest_input_by_player[input.player_id] = &input;
    ensure_player(input.player_id);
  }

  for (auto& [player_id, state] : states_by_player_) {
    game::MovementInputIntent intent{};
    auto input_it = latest_input_by_player.find(player_id);
    if (input_it != latest_input_by_player.end() && input_it->second != nullptr) {
      const PlayerInputCommand& input = *input_it->second;
      intent.move_x = input.move_x;
      intent.move_y = input.move_y;
      intent.jump = input.jump;
      intent.sprint = input.sprint;
      intent.crouch = input.crouch;
      if (input.input_seq > state.last_processed_input_seq) {
        state.last_processed_input_seq = input.input_seq;
      }
    }

    const game::MovementKinematicState stepped = game::step_movement(
        {state.position_x, state.position_y, state.velocity_x, state.velocity_y,
         state.vertical_position, state.vertical_velocity, state.grounded, state.move_state},
        intent, dt, config_.tuning);
    state.position_x = stepped.position_x;
    state.position_y = stepped.position_y;
    state.velocity_x = stepped.velocity_x;
    state.velocity_y = stepped.velocity_y;
    state.speed = game::horizontal_speed(stepped);
    state.grounded = stepped.grounded;
    state.move_state = stepped.move_state;
    state.vertical_position = stepped.vertical_position;
    state.vertical_velocity = stepped.vertical_velocity;
  }
}

std::optional<PlayerMotionState> MovementSimulation::state_for(uint32_t player_id) const {
  auto it = states_by_player_.find(player_id);
  if (it == states_by_player_.end()) {
    return std::nullopt;
  }
  return it->second;
}

std::vector<PlayerMotionState> MovementSimulation::snapshot() const {
  std::vector<PlayerMotionState> states{};
  states.reserve(states_by_player_.size());
  for (const auto& [player_id, state] : states_by_player_) {
    static_cast<void>(player_id);
    states.push_back(state);
  }

  std::sort(states.begin(), states.end(),
            [](const PlayerMotionState& lhs, const PlayerMotionState& rhs) {
              return lhs.player_id < rhs.player_id;
            });
  return states;
}

MovementConfig MovementSimulation::sanitize_config(MovementConfig config) {
  config.tuning = game::sanitize_movement_tuning(config.tuning);
  return config;
}

} // namespace devy::server
