#include "client/PredictionReconciler.h"
#include "server/MovementSimulation.h"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <deque>
#include <utility>
#include <vector>

namespace devy::client {
namespace {

using namespace std::chrono_literals;

TEST_CASE("Prediction reconciler replays only unacknowledged inputs after authoritative snapshot") {
  PredictionReconciler reconciler({6.0F, 32U});

  reconciler.queue_local_input(1.0F, 0.0F, false, false, 100ms);
  reconciler.queue_local_input(1.0F, 0.0F, false, false, 100ms);
  reconciler.queue_local_input(1.0F, 0.0F, false, false, 100ms);

  REQUIRE(reconciler.pending_input_count() == 3U);
  REQUIRE(reconciler.state().position_x == Catch::Approx(1.8F).margin(0.0001F));

  const SnapshotMotionState authoritative{
      1U, 1.0F, 0.0F, 6.0F, 0.0F, 2U,
  };

  const ReconciliationResult result = reconciler.reconcile(authoritative);
  REQUIRE(result.applied);
  REQUIRE(result.acked_input_seq == 2U);
  REQUIRE(result.replayed_inputs == 1U);
  REQUIRE(result.correction_distance == Catch::Approx(0.2F).margin(0.0001F));

  REQUIRE(reconciler.pending_input_count() == 1U);
  REQUIRE(reconciler.state().last_input_seq == 3U);
  REQUIRE(reconciler.state().position_x == Catch::Approx(1.6F).margin(0.0001F));
  REQUIRE(reconciler.state().velocity_x == Catch::Approx(6.0F).margin(0.0001F));
}

TEST_CASE("Prediction reconciler consumes snapshot payloads using last_processed_input_seq") {
  PredictionReconciler reconciler({6.0F, 8U});
  reconciler.queue_local_input(0.0F, 1.0F, false, false, 100ms);

  const nlohmann::json snapshot_payload = {
      {"tick", 5},
      {"players", nlohmann::json::array({{{"player_id", 42},
                                          {"player_name", "remote"},
                                          {"position", {{"x", 2.0}, {"y", 1.0}}},
                                          {"velocity", {{"x", 0.0}, {"y", 0.0}}},
                                          {"last_processed_input_seq", 0}},
                                         {{"player_id", 7},
                                          {"player_name", "local"},
                                          {"position", {{"x", 0.0}, {"y", 0.6}}},
                                          {"velocity", {{"x", 0.0}, {"y", 6.0}}},
                                          {"last_processed_input_seq", 1}}})}};

  const auto result = reconciler.consume_snapshot(snapshot_payload, 7U);
  REQUIRE(result.has_value());
  REQUIRE(result->acked_input_seq == 1U);
  REQUIRE(result->replayed_inputs == 0U);
  REQUIRE(reconciler.pending_input_count() == 0U);
  REQUIRE(reconciler.state().position_y == Catch::Approx(0.6F).margin(0.0001F));
  REQUIRE(reconciler.state().velocity_y == Catch::Approx(6.0F).margin(0.0001F));

  const auto missing = reconciler.consume_snapshot({{"tick", 9}}, 7U);
  REQUIRE_FALSE(missing.has_value());
}

struct SimulationMetrics {
  std::size_t reconciliation_samples{0U};
  float max_correction_distance{0.0F};
  float max_authoritative_drift{0.0F};
};

struct InFlightInput {
  uint64_t deliver_tick{0U};
  server::PlayerInputCommand command{};
};

struct InFlightSnapshot {
  uint64_t deliver_tick{0U};
  SnapshotMotionState state{};
};

bool should_drop(uint64_t ordinal, uint32_t loss_percent, uint64_t salt) {
  if (loss_percent == 0U) {
    return false;
  }
  const uint64_t mixed = ordinal * 1'103'515'245ULL + salt * 12'345ULL + 6'789ULL;
  const uint64_t bucket = mixed % 100ULL;
  return bucket < static_cast<uint64_t>(loss_percent);
}

std::pair<float, float> movement_for_tick(uint64_t tick) {
  const uint64_t phase = (tick / 20U) % 4U;
  switch (phase) {
  case 0U:
    return {1.0F, 0.0F};
  case 1U:
    return {0.0F, 1.0F};
  case 2U:
    return {-1.0F, 0.0F};
  default:
    return {0.0F, -1.0F};
  }
}

SimulationMetrics run_latency_loss_simulation(uint32_t latency_ms, uint32_t loss_percent) {
  constexpr uint32_t kPlayerId = 1U;
  constexpr auto kTickInterval = 50ms;
  constexpr uint64_t kTicks = 600U;

  const double one_way_ms = static_cast<double>(latency_ms) * 0.5;
  const double tick_ms = static_cast<double>(kTickInterval.count());
  const uint32_t one_way_ticks =
      std::max(1U, static_cast<uint32_t>(std::ceil(one_way_ms / tick_ms)));

  server::MovementSimulation server_simulation({6.0F});
  server::MovementSimulation baseline_simulation({6.0F});
  server_simulation.ensure_player(kPlayerId);
  baseline_simulation.ensure_player(kPlayerId);

  PredictionReconciler reconciler({6.0F, 512U});
  std::deque<InFlightInput> in_flight_inputs{};
  std::deque<InFlightSnapshot> in_flight_snapshots{};

  SimulationMetrics metrics{};
  for (uint64_t tick = 1U; tick <= kTicks; ++tick) {
    const auto [move_x, move_y] = movement_for_tick(tick);
    const uint32_t input_seq =
        reconciler.queue_local_input(move_x, move_y, false, false, kTickInterval);

    if (!should_drop(input_seq, loss_percent, 17ULL)) {
      in_flight_inputs.push_back(
          {tick + static_cast<uint64_t>(one_way_ticks),
           {kPlayerId, input_seq, move_x, move_y, false, false, server::RuntimeTimePoint{}}});
    }

    std::vector<server::PlayerInputCommand> delivered_inputs{};
    while (!in_flight_inputs.empty() && in_flight_inputs.front().deliver_tick <= tick) {
      delivered_inputs.push_back(in_flight_inputs.front().command);
      in_flight_inputs.pop_front();
    }
    server_simulation.apply_inputs(kTickInterval, delivered_inputs);
    baseline_simulation.apply_inputs(kTickInterval, {{kPlayerId, input_seq, move_x, move_y, false,
                                                      false, server::RuntimeTimePoint{}}});

    const auto authoritative_state = server_simulation.state_for(kPlayerId);
    REQUIRE(authoritative_state.has_value());

    const auto baseline_state = baseline_simulation.state_for(kPlayerId);
    REQUIRE(baseline_state.has_value());

    const double dropped_dx =
        static_cast<double>(authoritative_state->position_x - baseline_state->position_x);
    const double dropped_dy =
        static_cast<double>(authoritative_state->position_y - baseline_state->position_y);
    const float authoritative_drift =
        static_cast<float>(std::sqrt(dropped_dx * dropped_dx + dropped_dy * dropped_dy));
    metrics.max_authoritative_drift =
        std::max(metrics.max_authoritative_drift, authoritative_drift);

    if (!should_drop(tick, loss_percent, 71ULL)) {
      in_flight_snapshots.push_back(
          {tick + static_cast<uint64_t>(one_way_ticks),
           {kPlayerId, authoritative_state->position_x, authoritative_state->position_y,
            authoritative_state->velocity_x, authoritative_state->velocity_y,
            authoritative_state->last_processed_input_seq}});
    }

    while (!in_flight_snapshots.empty() && in_flight_snapshots.front().deliver_tick <= tick) {
      const ReconciliationResult result = reconciler.reconcile(in_flight_snapshots.front().state);
      metrics.max_correction_distance =
          std::max(metrics.max_correction_distance, result.correction_distance);
      ++metrics.reconciliation_samples;
      in_flight_snapshots.pop_front();
    }
  }

  return metrics;
}

TEST_CASE("Prediction reconciliation remains bounded across latency and packet loss matrix") {
  constexpr std::array<uint32_t, 3> kLatenciesMs{50U, 100U, 200U};
  constexpr std::array<uint32_t, 3> kLossPercent{1U, 3U, 5U};

  for (const uint32_t latency : kLatenciesMs) {
    for (const uint32_t loss : kLossPercent) {
      const SimulationMetrics metrics = run_latency_loss_simulation(latency, loss);
      INFO("latency_ms=" << latency << " loss_percent=" << loss
                         << " max_correction=" << metrics.max_correction_distance
                         << " max_authoritative_drift=" << metrics.max_authoritative_drift
                         << " samples=" << metrics.reconciliation_samples);

      REQUIRE(metrics.reconciliation_samples > 0U);
      REQUIRE(metrics.max_correction_distance <= 2.5F);
      REQUIRE(metrics.max_authoritative_drift <= 2.5F);
    }
  }
}

} // namespace
} // namespace devy::client
