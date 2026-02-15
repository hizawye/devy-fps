#include "server/RuntimeTelemetry.h"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>
#include <nlohmann/json.hpp>

#include <algorithm>
#include <chrono>
#include <string>

namespace devy::server {
namespace {

TEST_CASE("Runtime telemetry emits configured interval reports with tick and payload metrics") {
  RuntimeTelemetry telemetry({true, 3U, 3U});
  const auto t0 = std::chrono::steady_clock::time_point{};

  for (uint64_t tick = 1U; tick <= 3U; ++tick) {
    const auto started_at = t0 + std::chrono::milliseconds(static_cast<int64_t>(tick) * 10);
    telemetry.begin_tick(tick, started_at);
    telemetry.note_phase(RuntimeTickPhase::Movement, std::chrono::milliseconds(tick));
    telemetry.note_phase(RuntimeTickPhase::Combat, std::chrono::milliseconds(tick * 2U));
    telemetry.note_resources({static_cast<std::size_t>(tick), static_cast<std::size_t>(tick + 1U),
                              static_cast<std::size_t>(tick * 2U),
                              static_cast<std::size_t>(tick * 3U),
                              static_cast<std::size_t>(tick * 4U)});
    telemetry.note_outbound_packet(devy::net::MessageType::StateSnapshot,
                                   static_cast<std::size_t>(tick * 100U));

    const auto report =
        telemetry.end_tick(started_at + std::chrono::milliseconds(static_cast<int64_t>(tick)));
    if (tick < 3U) {
      REQUIRE_FALSE(report.has_value());
      continue;
    }

    REQUIRE(report.has_value());
    REQUIRE(report->tick == 3U);
    REQUIRE(report->ticks_in_window == 3U);
    REQUIRE(report->tick_avg_ms == Catch::Approx(2.0));
    REQUIRE(report->tick_p95_ms == Catch::Approx(3.0));
    REQUIRE(report->tick_max_ms == Catch::Approx(3.0));

    REQUIRE(report->phase_avg_ms[static_cast<std::size_t>(RuntimeTickPhase::Movement)] ==
            Catch::Approx(2.0));
    REQUIRE(report->phase_max_ms[static_cast<std::size_t>(RuntimeTickPhase::Combat)] ==
            Catch::Approx(6.0));

    const auto snapshot_index = static_cast<std::size_t>(devy::net::MessageType::StateSnapshot);
    REQUIRE(report->packet_count_by_type[snapshot_index] == 3U);
    REQUIRE(report->packet_bytes_by_type[snapshot_index] == 600U);
    REQUIRE(report->total_packets_sent == 3U);
    REQUIRE(report->total_packet_bytes == 600U);

    REQUIRE(report->peak_active_sessions == 3U);
    REQUIRE(report->peak_tracked_players == 4U);
    REQUIRE(report->peak_pending_inputs == 6U);
    REQUIRE(report->peak_pending_snapshot_events == 9U);
    REQUIRE(report->peak_active_treasure_spawns == 12U);
    REQUIRE(report->peak_estimated_state_bytes > 0U);
  }
}

TEST_CASE("Runtime telemetry clears rolling counters after each report window") {
  RuntimeTelemetry telemetry({true, 2U, 2U});
  const auto t0 = std::chrono::steady_clock::time_point{};

  telemetry.begin_tick(1U, t0);
  telemetry.note_outbound_packet(devy::net::MessageType::StateSnapshot, 128U);
  REQUIRE_FALSE(telemetry.end_tick(t0 + std::chrono::milliseconds(1)).has_value());

  telemetry.begin_tick(2U, t0 + std::chrono::milliseconds(2));
  telemetry.note_outbound_packet(devy::net::MessageType::StateSnapshot, 256U);
  const auto first_report = telemetry.end_tick(t0 + std::chrono::milliseconds(3));
  REQUIRE(first_report.has_value());
  REQUIRE(first_report->total_packets_sent == 2U);
  REQUIRE(first_report->total_packet_bytes == 384U);

  telemetry.begin_tick(3U, t0 + std::chrono::milliseconds(4));
  telemetry.note_outbound_packet(devy::net::MessageType::MatchState, 64U);
  REQUIRE_FALSE(telemetry.end_tick(t0 + std::chrono::milliseconds(5)).has_value());

  telemetry.begin_tick(4U, t0 + std::chrono::milliseconds(6));
  telemetry.note_outbound_packet(devy::net::MessageType::MatchState, 32U);
  const auto second_report = telemetry.end_tick(t0 + std::chrono::milliseconds(7));
  REQUIRE(second_report.has_value());
  REQUIRE(second_report->tick == 4U);
  REQUIRE(second_report->total_packets_sent == 2U);
  REQUIRE(second_report->total_packet_bytes == 96U);

  const auto snapshot_index = static_cast<std::size_t>(devy::net::MessageType::StateSnapshot);
  const auto match_state_index = static_cast<std::size_t>(devy::net::MessageType::MatchState);
  REQUIRE(second_report->packet_count_by_type[snapshot_index] == 0U);
  REQUIRE(second_report->packet_count_by_type[match_state_index] == 2U);
}

TEST_CASE("Runtime telemetry stays silent when disabled") {
  RuntimeTelemetry telemetry({false, 1U, 1U});
  const auto t0 = std::chrono::steady_clock::time_point{};

  telemetry.begin_tick(1U, t0);
  telemetry.note_phase(RuntimeTickPhase::Movement, std::chrono::milliseconds(1));
  telemetry.note_outbound_packet(devy::net::MessageType::StateSnapshot, 256U);
  telemetry.note_resources({2U, 2U, 2U, 2U, 2U});
  REQUIRE_FALSE(telemetry.end_tick(t0 + std::chrono::milliseconds(1)).has_value());
}

TEST_CASE("Runtime telemetry reports diagnostics counters and threshold alerts") {
  RuntimeTelemetry telemetry({true, 4U, 4U, 0.0, {0.20, 0.30, 0.20, 2U}});
  telemetry.set_tick_budget(std::chrono::milliseconds(10));
  const auto t0 = std::chrono::steady_clock::time_point{};

  for (uint64_t tick = 1U; tick <= 4U; ++tick) {
    const auto started_at = t0 + std::chrono::milliseconds(static_cast<int64_t>(tick) * 20);
    telemetry.begin_tick(tick, started_at);
    telemetry.note_resources({static_cast<std::size_t>(tick >= 3U ? 2U : 1U), 0U, 0U, 0U, 0U});
    telemetry.note_inbound_packet_received();
    if (tick == 2U) {
      telemetry.note_inbound_packet_drop(true);
    }
    if (tick == 3U) {
      telemetry.note_inbound_packet_drop(false);
      telemetry.note_command_rejected();
    }

    const auto elapsed =
        (tick == 2U) ? std::chrono::milliseconds(14) : std::chrono::milliseconds(10);
    const auto report = telemetry.end_tick(started_at + elapsed);
    if (tick < 4U) {
      REQUIRE_FALSE(report.has_value());
      continue;
    }

    REQUIRE(report.has_value());
    REQUIRE(report->tick_lag_count == 1U);
    REQUIRE(report->tick_lag_rate == Catch::Approx(0.25));
    REQUIRE(report->max_tick_overrun_ms == Catch::Approx(4.0));
    REQUIRE(report->inbound_packets_received == 4U);
    REQUIRE(report->inbound_packets_dropped == 2U);
    REQUIRE(report->inbound_parse_errors == 1U);
    REQUIRE(report->command_rejections == 1U);
    REQUIRE(report->inbound_packet_drop_rate == Catch::Approx(0.5));
    REQUIRE(report->inbound_parse_error_rate == Catch::Approx(0.25));
    REQUIRE(report->avg_active_players == Catch::Approx(1.5));
    REQUIRE(report->last_active_players == 2U);
    REQUIRE(report->alerts.size() == 4U);

    REQUIRE(std::find(report->alerts.begin(), report->alerts.end(),
                      "tick_lag_rate_exceeded") != report->alerts.end());
    REQUIRE(std::find(report->alerts.begin(), report->alerts.end(),
                      "packet_drop_rate_exceeded") != report->alerts.end());
    REQUIRE(std::find(report->alerts.begin(), report->alerts.end(),
                      "parse_error_rate_exceeded") != report->alerts.end());
    REQUIRE(std::find(report->alerts.begin(), report->alerts.end(),
                      "active_players_below_min") != report->alerts.end());

    const auto diagnostics_line = format_runtime_diagnostics_json(report.value());
    REQUIRE(diagnostics_line.find("Runtime diagnostics json=") == 0U);
    const auto diagnostics =
        nlohmann::json::parse(diagnostics_line.substr(std::string("Runtime diagnostics json=").size()));
    REQUIRE(diagnostics["kind"] == "runtime_diagnostics_v1");
    REQUIRE(diagnostics["alerts"].is_array());
    REQUIRE(diagnostics["alerts"].size() == 4U);
  }
}

} // namespace
} // namespace devy::server
