#include "server/MovementSimulation.h"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <utility>

namespace devy::server {
namespace {

constexpr float kDefaultMaxSpeedUnitsPerSecond = 6.0F;
constexpr float kInputMagnitudeEpsilon = 0.000001F;

float sanitize_axis(float axis) {
  if (!std::isfinite(axis)) {
    return 0.0F;
  }
  return std::clamp(axis, -1.0F, 1.0F);
}

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
  for (auto& [player_id, state] : states_by_player_) {
    static_cast<void>(player_id);
    state.velocity_x = 0.0F;
    state.velocity_y = 0.0F;
  }

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
  }

  for (const auto& [player_id, input_ptr] : latest_input_by_player) {
    if (!input_ptr) {
      continue;
    }

    ensure_player(player_id);
    auto state_it = states_by_player_.find(player_id);
    if (state_it == states_by_player_.end()) {
      continue;
    }
    PlayerMotionState& state = state_it->second;

    const float axis_x = sanitize_axis(input_ptr->move_x);
    const float axis_y = sanitize_axis(input_ptr->move_y);
    const double magnitude_sq = static_cast<double>(axis_x) * static_cast<double>(axis_x) +
                                static_cast<double>(axis_y) * static_cast<double>(axis_y);

    if (magnitude_sq > static_cast<double>(kInputMagnitudeEpsilon)) {
      const float magnitude = static_cast<float>(std::sqrt(magnitude_sq));
      const float axis_scale = (magnitude > 1.0F) ? (1.0F / magnitude) : 1.0F;
      const float dir_x = axis_x * axis_scale;
      const float dir_y = axis_y * axis_scale;
      state.velocity_x = dir_x * config_.max_speed_units_per_second;
      state.velocity_y = dir_y * config_.max_speed_units_per_second;
      state.position_x += state.velocity_x * dt;
      state.position_y += state.velocity_y * dt;
    }

    if (input_ptr->input_seq > state.last_processed_input_seq) {
      state.last_processed_input_seq = input_ptr->input_seq;
    }
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
  if (!std::isfinite(config.max_speed_units_per_second) ||
      config.max_speed_units_per_second <= 0.0F) {
    config.max_speed_units_per_second = kDefaultMaxSpeedUnitsPerSecond;
  }
  return config;
}

} // namespace devy::server
