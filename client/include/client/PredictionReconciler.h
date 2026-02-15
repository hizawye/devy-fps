#pragma once

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <deque>
#include <optional>

#include <nlohmann/json.hpp>

namespace devy::client {

struct PredictionConfig {
  float max_speed_units_per_second{6.0F};
  std::size_t max_pending_inputs{256U};
};

struct PredictedMotionState {
  float position_x{0.0F};
  float position_y{0.0F};
  float velocity_x{0.0F};
  float velocity_y{0.0F};
  uint32_t last_input_seq{0U};
};

struct SnapshotMotionState {
  uint32_t player_id{0U};
  float position_x{0.0F};
  float position_y{0.0F};
  float velocity_x{0.0F};
  float velocity_y{0.0F};
  uint32_t last_processed_input_seq{0U};
};

struct ReconciliationResult {
  bool applied{false};
  uint32_t acked_input_seq{0U};
  std::size_t replayed_inputs{0U};
  float correction_distance{0.0F};
};

class PredictionReconciler {
public:
  explicit PredictionReconciler(PredictionConfig config = {});

  void reset();
  uint32_t queue_local_input(float move_x, float move_y, bool jump, bool fire,
                             std::chrono::nanoseconds dt);
  ReconciliationResult reconcile(const SnapshotMotionState& authoritative_state);
  std::optional<ReconciliationResult> consume_snapshot(const nlohmann::json& snapshot_payload,
                                                       uint32_t local_player_id);

  [[nodiscard]] const PredictedMotionState& state() const;
  [[nodiscard]] std::size_t pending_input_count() const;
  [[nodiscard]] uint32_t next_input_seq() const;

  [[nodiscard]] static std::optional<SnapshotMotionState>
  extract_player_state(const nlohmann::json& snapshot_payload, uint32_t player_id);

private:
  struct PendingInput {
    uint32_t input_seq{0U};
    float move_x{0.0F};
    float move_y{0.0F};
    bool jump{false};
    bool fire{false};
    std::chrono::nanoseconds dt{};
  };

  [[nodiscard]] static PredictionConfig sanitize_config(PredictionConfig config);
  static void apply_input(PredictedMotionState& state, const PendingInput& input,
                          float max_speed_units_per_second);

  PredictionConfig config_{};
  uint32_t next_input_seq_{1U};
  PredictedMotionState state_{};
  std::deque<PendingInput> pending_inputs_{};
};

} // namespace devy::client
