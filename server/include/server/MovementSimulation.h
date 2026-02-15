#pragma once

#include "server/AuthoritativeLoop.h"

#include <chrono>
#include <cstdint>
#include <optional>
#include <unordered_map>
#include <vector>

namespace devy::server {

struct MovementConfig {
  float max_speed_units_per_second{6.0F};
};

struct PlayerMotionState {
  uint32_t player_id{0};
  float position_x{0.0F};
  float position_y{0.0F};
  float velocity_x{0.0F};
  float velocity_y{0.0F};
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
