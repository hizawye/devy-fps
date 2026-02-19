#pragma once

#include "server/AuthoritativeLoop.h"
#include "shared/game/Movement.h"

#include <chrono>
#include <cstdint>
#include <optional>
#include <unordered_map>
#include <vector>

namespace devy::server {

struct MovementConfig {
  devy::game::MovementTuning tuning{};

  MovementConfig() = default;
  explicit MovementConfig(float max_speed_units_per_second) {
    tuning.max_speed_walk_units_per_second = max_speed_units_per_second;
  }
  explicit MovementConfig(devy::game::MovementTuning movement_tuning) : tuning(movement_tuning) {}
};

struct PlayerMotionState {
  uint32_t player_id{0};
  float position_x{0.0F};
  float position_y{0.0F};
  float velocity_x{0.0F};
  float velocity_y{0.0F};
  float speed{0.0F};
  bool grounded{true};
  devy::game::MoveState move_state{devy::game::MoveState::Idle};
  float vertical_position{0.0F};
  float vertical_velocity{0.0F};
  uint32_t last_processed_input_seq{0};
};

class MovementSimulation {
public:
  explicit MovementSimulation(MovementConfig config = {});

  void reset();
  void ensure_player(uint32_t player_id);
  void remove_player(uint32_t player_id);
  void apply_inputs(std::chrono::nanoseconds tick_interval,
                    const std::vector<PlayerInputCommand>& inputs);
  [[nodiscard]] std::optional<PlayerMotionState> state_for(uint32_t player_id) const;
  [[nodiscard]] std::vector<PlayerMotionState> snapshot() const;

private:
  [[nodiscard]] static MovementConfig sanitize_config(MovementConfig config);

  MovementConfig config_{};
  std::unordered_map<uint32_t, PlayerMotionState> states_by_player_{};
};

} // namespace devy::server
