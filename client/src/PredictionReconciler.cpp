#include "client/PredictionReconciler.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <string>
#include <utility>

namespace devy::client {
namespace {

constexpr std::size_t kDefaultMaxPendingInputs = 256U;

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

devy::game::MoveState parse_move_state(const nlohmann::json& player) {
  if (!player.contains("move_state") || !player["move_state"].is_string()) {
    return devy::game::MoveState::Idle;
  }
  const std::string move_state = player["move_state"].get<std::string>();
  if (move_state == "walk") {
    return devy::game::MoveState::Walk;
  }
  if (move_state == "sprint") {
    return devy::game::MoveState::Sprint;
  }
  if (move_state == "crouch") {
    return devy::game::MoveState::Crouch;
  }
  if (move_state == "air") {
    return devy::game::MoveState::Air;
  }
  return devy::game::MoveState::Idle;
}

} // namespace

PredictionReconciler::PredictionReconciler(PredictionConfig config)
    : config_(sanitize_config(config)) {}

void PredictionReconciler::reset() {
  next_input_seq_ = 1U;
  state_ = {};
  pending_inputs_.clear();
}

uint32_t PredictionReconciler::queue_local_input(float move_x, float move_y, bool jump, bool sprint,
                                                 bool crouch, bool fire,
                                                 std::chrono::nanoseconds dt) {
  PendingInput input{};
  input.input_seq = next_input_seq_++;
  input.move_x = move_x;
  input.move_y = move_y;
  input.jump = jump;
  input.sprint = sprint;
  input.crouch = crouch;
  input.fire = fire;
  input.dt = dt;

  if (pending_inputs_.size() >= config_.max_pending_inputs) {
    pending_inputs_.pop_front();
  }
  pending_inputs_.push_back(input);
  apply_input(state_, input, config_.movement_tuning);
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
  state_.speed = authoritative_state.speed;
  state_.grounded = authoritative_state.grounded;
  state_.move_state = authoritative_state.move_state;
  state_.vertical_position = authoritative_state.vertical_position;
  state_.vertical_velocity = authoritative_state.vertical_velocity;
  state_.last_input_seq = authoritative_state.last_processed_input_seq;

  std::size_t replayed_inputs = 0U;
  for (const auto& input : pending_inputs_) {
    apply_input(state_, input, config_.movement_tuning);
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
    state.speed = json_to_float(player.value("speed", nlohmann::json{}))
                      .value_or(std::sqrt((state.velocity_x * state.velocity_x) +
                                          (state.velocity_y * state.velocity_y)));
    state.grounded = player.contains("grounded") && player["grounded"].is_boolean()
                         ? player["grounded"].get<bool>()
                         : true;
    state.move_state = parse_move_state(player);
    state.vertical_position =
        json_to_float(player.value("vertical_position", nlohmann::json{})).value_or(0.0F);
    state.vertical_velocity =
        json_to_float(player.value("vertical_velocity", nlohmann::json{})).value_or(0.0F);
    state.last_processed_input_seq = last_processed_input_seq.value();
    return state;
  }

  return std::nullopt;
}

PredictionConfig PredictionReconciler::sanitize_config(PredictionConfig config) {
  config.movement_tuning = game::sanitize_movement_tuning(config.movement_tuning);
  if (config.max_pending_inputs == 0U) {
    config.max_pending_inputs = kDefaultMaxPendingInputs;
  }
  return config;
}

void PredictionReconciler::apply_input(PredictedMotionState& state, const PendingInput& input,
                                       const game::MovementTuning& tuning) {
  static_cast<void>(input.fire);

  const double dt_seconds = static_cast<double>(input.dt.count()) / 1'000'000'000.0;
  const game::MovementKinematicState stepped = game::step_movement(
      {state.position_x, state.position_y, state.velocity_x, state.velocity_y,
       state.vertical_position, state.vertical_velocity, state.grounded, state.move_state},
      {input.move_x, input.move_y, input.jump, input.sprint, input.crouch},
      static_cast<float>(dt_seconds), tuning);

  state.position_x = stepped.position_x;
  state.position_y = stepped.position_y;
  state.velocity_x = stepped.velocity_x;
  state.velocity_y = stepped.velocity_y;
  state.speed = game::horizontal_speed(stepped);
  state.grounded = stepped.grounded;
  state.move_state = stepped.move_state;
  state.vertical_position = stepped.vertical_position;
  state.vertical_velocity = stepped.vertical_velocity;
  state.last_input_seq = std::max(state.last_input_seq, input.input_seq);
}

} // namespace devy::client
