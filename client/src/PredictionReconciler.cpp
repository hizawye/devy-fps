#include "client/PredictionReconciler.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <utility>

namespace devy::client {
namespace {

constexpr float kDefaultMaxSpeedUnitsPerSecond = 6.0F;
constexpr std::size_t kDefaultMaxPendingInputs = 256U;
constexpr float kInputMagnitudeEpsilon = 0.000001F;

float sanitize_axis(float axis) {
  if (!std::isfinite(axis)) {
    return 0.0F;
  }
  return std::clamp(axis, -1.0F, 1.0F);
}

std::optional<uint32_t> json_to_u32(const nlohmann::json& value) {
  if (value.is_number_unsigned()) {
    const uint64_t raw = value.get<uint64_t>();
    if (raw <= static_cast<uint64_t>(std::numeric_limits<uint32_t>::max())) {
      return static_cast<uint32_t>(raw);
    }
    return std::nullopt;
  }

  if (value.is_number_integer()) {
    const int64_t raw = value.get<int64_t>();
    if (raw >= 0 && raw <= static_cast<int64_t>(std::numeric_limits<uint32_t>::max())) {
      return static_cast<uint32_t>(raw);
    }
    return std::nullopt;
  }

  return std::nullopt;
}

std::optional<float> json_to_float(const nlohmann::json& value) {
  if (!value.is_number()) {
    return std::nullopt;
  }

  const double raw = value.get<double>();
  if (raw < static_cast<double>(std::numeric_limits<float>::lowest()) ||
      raw > static_cast<double>(std::numeric_limits<float>::max())) {
    return std::nullopt;
  }
  return static_cast<float>(raw);
}

} // namespace

PredictionReconciler::PredictionReconciler(PredictionConfig config)
    : config_(sanitize_config(config)) {}

void PredictionReconciler::reset() {
  next_input_seq_ = 1U;
  state_ = {};
  pending_inputs_.clear();
}

uint32_t PredictionReconciler::queue_local_input(float move_x, float move_y, bool jump, bool fire,
                                                 std::chrono::nanoseconds dt) {
  PendingInput input{};
  input.input_seq = next_input_seq_++;
  input.move_x = move_x;
  input.move_y = move_y;
  input.jump = jump;
  input.fire = fire;
  input.dt = dt;

  if (pending_inputs_.size() >= config_.max_pending_inputs) {
    pending_inputs_.pop_front();
  }
  pending_inputs_.push_back(input);
  apply_input(state_, input, config_.max_speed_units_per_second);
  return input.input_seq;
}

ReconciliationResult
PredictionReconciler::reconcile(const SnapshotMotionState& authoritative_state) {
  const float before_x = state_.position_x;
  const float before_y = state_.position_y;

  while (!pending_inputs_.empty() &&
         pending_inputs_.front().input_seq <= authoritative_state.last_processed_input_seq) {
    pending_inputs_.pop_front();
  }

  state_.position_x = authoritative_state.position_x;
  state_.position_y = authoritative_state.position_y;
  state_.velocity_x = authoritative_state.velocity_x;
  state_.velocity_y = authoritative_state.velocity_y;
  state_.last_input_seq = authoritative_state.last_processed_input_seq;

  std::size_t replayed_inputs = 0U;
  for (const auto& input : pending_inputs_) {
    apply_input(state_, input, config_.max_speed_units_per_second);
    ++replayed_inputs;
  }

  const double dx = static_cast<double>(state_.position_x - before_x);
  const double dy = static_cast<double>(state_.position_y - before_y);
  const double correction = std::sqrt(dx * dx + dy * dy);

  ReconciliationResult result{};
  result.applied = true;
  result.acked_input_seq = authoritative_state.last_processed_input_seq;
  result.replayed_inputs = replayed_inputs;
  result.correction_distance = static_cast<float>(correction);
  return result;
}

std::optional<ReconciliationResult>
PredictionReconciler::consume_snapshot(const nlohmann::json& snapshot_payload,
                                       uint32_t local_player_id) {
  const auto player_state = extract_player_state(snapshot_payload, local_player_id);
  if (!player_state.has_value()) {
    return std::nullopt;
  }
  return reconcile(player_state.value());
}

const PredictedMotionState& PredictionReconciler::state() const { return state_; }

std::size_t PredictionReconciler::pending_input_count() const { return pending_inputs_.size(); }

uint32_t PredictionReconciler::next_input_seq() const { return next_input_seq_; }

std::optional<SnapshotMotionState>
PredictionReconciler::extract_player_state(const nlohmann::json& snapshot_payload,
                                           uint32_t player_id) {
  if (!snapshot_payload.is_object() || !snapshot_payload.contains("players") ||
      !snapshot_payload["players"].is_array()) {
    return std::nullopt;
  }

  for (const auto& player : snapshot_payload["players"]) {
    if (!player.is_object()) {
      continue;
    }

    if (!player.contains("player_id") || !player.contains("position") ||
        !player.contains("velocity") || !player.contains("last_processed_input_seq")) {
      continue;
    }

    const auto parsed_player_id = json_to_u32(player["player_id"]);
    if (!parsed_player_id.has_value() || parsed_player_id.value() != player_id) {
      continue;
    }

    if (!player["position"].is_object() || !player["velocity"].is_object()) {
      return std::nullopt;
    }

    if (!player["position"].contains("x") || !player["position"].contains("y") ||
        !player["velocity"].contains("x") || !player["velocity"].contains("y")) {
      return std::nullopt;
    }

    const auto position_x = json_to_float(player["position"]["x"]);
    const auto position_y = json_to_float(player["position"]["y"]);
    const auto velocity_x = json_to_float(player["velocity"]["x"]);
    const auto velocity_y = json_to_float(player["velocity"]["y"]);
    const auto last_processed_input_seq = json_to_u32(player["last_processed_input_seq"]);
    if (!position_x.has_value() || !position_y.has_value() || !velocity_x.has_value() ||
        !velocity_y.has_value() || !last_processed_input_seq.has_value()) {
      return std::nullopt;
    }

    SnapshotMotionState state{};
    state.player_id = parsed_player_id.value();
    state.position_x = position_x.value();
    state.position_y = position_y.value();
    state.velocity_x = velocity_x.value();
    state.velocity_y = velocity_y.value();
    state.last_processed_input_seq = last_processed_input_seq.value();
    return state;
  }

  return std::nullopt;
}

PredictionConfig PredictionReconciler::sanitize_config(PredictionConfig config) {
  if (!std::isfinite(config.max_speed_units_per_second) ||
      config.max_speed_units_per_second <= 0.0F) {
    config.max_speed_units_per_second = kDefaultMaxSpeedUnitsPerSecond;
  }
  if (config.max_pending_inputs == 0U) {
    config.max_pending_inputs = kDefaultMaxPendingInputs;
  }
  return config;
}

void PredictionReconciler::apply_input(PredictedMotionState& state, const PendingInput& input,
                                       float max_speed_units_per_second) {
  static_cast<void>(input.jump);
  static_cast<void>(input.fire);

  state.velocity_x = 0.0F;
  state.velocity_y = 0.0F;

  if (input.dt <= std::chrono::nanoseconds::zero()) {
    state.last_input_seq = std::max(state.last_input_seq, input.input_seq);
    return;
  }

  const double dt_seconds = static_cast<double>(input.dt.count()) / 1'000'000'000.0;
  const float dt = static_cast<float>(dt_seconds);
  if (dt <= 0.0F) {
    state.last_input_seq = std::max(state.last_input_seq, input.input_seq);
    return;
  }

  const float axis_x = sanitize_axis(input.move_x);
  const float axis_y = sanitize_axis(input.move_y);
  const double magnitude_sq = static_cast<double>(axis_x) * static_cast<double>(axis_x) +
                              static_cast<double>(axis_y) * static_cast<double>(axis_y);

  if (magnitude_sq > static_cast<double>(kInputMagnitudeEpsilon)) {
    const float magnitude = static_cast<float>(std::sqrt(magnitude_sq));
    const float axis_scale = (magnitude > 1.0F) ? (1.0F / magnitude) : 1.0F;
    const float dir_x = axis_x * axis_scale;
    const float dir_y = axis_y * axis_scale;
    state.velocity_x = dir_x * max_speed_units_per_second;
    state.velocity_y = dir_y * max_speed_units_per_second;
    state.position_x += state.velocity_x * dt;
    state.position_y += state.velocity_y * dt;
  }

  state.last_input_seq = std::max(state.last_input_seq, input.input_seq);
}

} // namespace devy::client
