#include "server/RuntimeTelemetry.h"

#include <algorithm>
#include <cmath>
#include <iomanip>
#include <limits>
#include <nlohmann/json.hpp>
#include <numeric>
#include <sstream>

namespace devy::server {
namespace {

double ns_to_ms(int64_t nanoseconds) { return static_cast<double>(nanoseconds) / 1'000'000.0; }

double ns_to_ms(uint64_t nanoseconds) { return static_cast<double>(nanoseconds) / 1'000'000.0; }

std::size_t percentile_index(std::size_t count, double percentile) {
  if (count == 0U) {
    return 0U;
  }
  const double rank = percentile * static_cast<double>(count);
  std::size_t index = static_cast<std::size_t>(std::ceil(rank));
  if (index == 0U) {
    index = 1U;
  }
  index -= 1U;
  if (index >= count) {
    index = count - 1U;
  }
  return index;
}

std::string format_ms(double value) {
  std::ostringstream out{};
  out << std::fixed << std::setprecision(3) << value;
  return out.str();
}

std::string format_percent(double ratio) {
  std::ostringstream out{};
  out << std::fixed << std::setprecision(2) << (ratio * 100.0);
  return out.str();
}

double sanitize_ratio(double value, double fallback) {
  if (!std::isfinite(value)) {
    return fallback;
  }
  return std::clamp(value, 0.0, 1.0);
}

int64_t ms_to_ns(double milliseconds) {
  if (!std::isfinite(milliseconds) || milliseconds <= 0.0) {
    return 0;
  }
  const double nanoseconds = milliseconds * 1'000'000.0;
  const double max_ns = static_cast<double>(std::numeric_limits<int64_t>::max());
  if (nanoseconds >= max_ns) {
    return std::numeric_limits<int64_t>::max();
  }
  return static_cast<int64_t>(nanoseconds);
}

} // namespace

RuntimeTelemetry::RuntimeTelemetry(RuntimeTelemetryConfig config)
    : config_(sanitize_config(config)),
      tick_lag_tolerance_ns_(ms_to_ns(config_.tick_lag_tolerance_ms)) {}

void RuntimeTelemetry::reset() {
  tick_active_ = false;
  current_tick_ = 0U;
  tick_started_at_ = {};
  ticks_since_report_ = 0U;
  tick_lag_count_ = 0U;
  max_tick_overrun_ns_ = 0;
  tick_durations_ns_.clear();
  phase_total_ns_.fill(0);
  phase_max_ns_.fill(0);
  inbound_packets_received_ = 0U;
  inbound_packets_dropped_ = 0U;
  inbound_parse_errors_ = 0U;
  command_rejections_ = 0U;
  packet_count_by_type_.fill(0U);
  packet_bytes_by_type_.fill(0U);
  total_packets_sent_ = 0U;
  total_packet_bytes_ = 0U;
  peak_active_sessions_ = 0U;
  peak_tracked_players_ = 0U;
  peak_pending_inputs_ = 0U;
  peak_pending_snapshot_events_ = 0U;
  peak_active_treasure_spawns_ = 0U;
  peak_estimated_state_bytes_ = 0U;
  active_players_sum_ = 0U;
  active_players_samples_ = 0U;
  last_active_players_ = 0U;
}

void RuntimeTelemetry::set_tick_budget(std::chrono::nanoseconds tick_budget) {
  int64_t budget_ns = tick_budget.count();
  if (budget_ns < 0) {
    budget_ns = 0;
  }
  tick_budget_ns_ = budget_ns;
}

void RuntimeTelemetry::begin_tick(uint64_t tick, std::chrono::steady_clock::time_point started_at) {
  if (!config_.enabled) {
    return;
  }
  current_tick_ = tick;
  tick_started_at_ = started_at;
  tick_active_ = true;
}

void RuntimeTelemetry::note_phase(RuntimeTickPhase phase, std::chrono::nanoseconds duration) {
  if (!config_.enabled || !tick_active_) {
    return;
  }
  const std::size_t index = phase_index(phase);
  int64_t elapsed = duration.count();
  if (elapsed < 0) {
    elapsed = 0;
  }
  phase_total_ns_[index] += elapsed;
  phase_max_ns_[index] = std::max(phase_max_ns_[index], elapsed);
}

void RuntimeTelemetry::note_resources(const RuntimeResourceSnapshot& resources) {
  if (!config_.enabled || !tick_active_) {
    return;
  }
  peak_active_sessions_ = std::max(peak_active_sessions_, resources.active_sessions);
  peak_tracked_players_ = std::max(peak_tracked_players_, resources.tracked_players);
  peak_pending_inputs_ = std::max(peak_pending_inputs_, resources.pending_inputs);
  peak_pending_snapshot_events_ =
      std::max(peak_pending_snapshot_events_, resources.pending_snapshot_events);
  peak_active_treasure_spawns_ =
      std::max(peak_active_treasure_spawns_, resources.active_treasure_spawns);
  peak_estimated_state_bytes_ =
      std::max(peak_estimated_state_bytes_, estimate_state_bytes(resources));
  active_players_sum_ += static_cast<uint64_t>(resources.active_sessions);
  ++active_players_samples_;
  last_active_players_ = resources.active_sessions;
}

void RuntimeTelemetry::note_inbound_packet_received() {
  if (!config_.enabled) {
    return;
  }
  inbound_packets_received_ += 1U;
}

void RuntimeTelemetry::note_inbound_packet_drop(bool parse_error) {
  if (!config_.enabled) {
    return;
  }
  inbound_packets_dropped_ += 1U;
  if (parse_error) {
    inbound_parse_errors_ += 1U;
  }
}

void RuntimeTelemetry::note_command_rejected() {
  if (!config_.enabled) {
    return;
  }
  command_rejections_ += 1U;
}

void RuntimeTelemetry::note_outbound_packet(devy::net::MessageType type,
                                            std::size_t payload_size_bytes) {
  if (!config_.enabled) {
    return;
  }
  const std::size_t index = message_type_index(type);
  packet_count_by_type_[index] += 1U;
  packet_bytes_by_type_[index] += static_cast<uint64_t>(payload_size_bytes);
  total_packets_sent_ += 1U;
  total_packet_bytes_ += static_cast<uint64_t>(payload_size_bytes);
}

std::optional<RuntimeTelemetryReport>
RuntimeTelemetry::end_tick(std::chrono::steady_clock::time_point finished_at) {
  if (!config_.enabled || !tick_active_) {
    return std::nullopt;
  }

  int64_t elapsed =
      std::chrono::duration_cast<std::chrono::nanoseconds>(finished_at - tick_started_at_).count();
  if (elapsed < 0) {
    elapsed = 0;
  }
  tick_durations_ns_.push_back(elapsed);
  if (tick_durations_ns_.size() > config_.history_size_ticks) {
    tick_durations_ns_.erase(tick_durations_ns_.begin());
  }

  if (tick_budget_ns_ > 0) {
    int64_t lag_threshold_ns = tick_budget_ns_;
    if (tick_lag_tolerance_ns_ > 0 &&
        lag_threshold_ns <= std::numeric_limits<int64_t>::max() - tick_lag_tolerance_ns_) {
      lag_threshold_ns += tick_lag_tolerance_ns_;
    } else if (tick_lag_tolerance_ns_ > 0) {
      lag_threshold_ns = std::numeric_limits<int64_t>::max();
    }

    if (elapsed > lag_threshold_ns) {
      ++tick_lag_count_;
      max_tick_overrun_ns_ = std::max(max_tick_overrun_ns_, elapsed - tick_budget_ns_);
    }
  }

  tick_active_ = false;
  ++ticks_since_report_;
  if (ticks_since_report_ < config_.report_interval_ticks) {
    return std::nullopt;
  }

  RuntimeTelemetryReport report = build_report();
  report.tick = current_tick_;
  report.ticks_in_window = ticks_since_report_;

  ticks_since_report_ = 0U;
  tick_lag_count_ = 0U;
  max_tick_overrun_ns_ = 0;
  tick_durations_ns_.clear();
  phase_total_ns_.fill(0);
  phase_max_ns_.fill(0);
  inbound_packets_received_ = 0U;
  inbound_packets_dropped_ = 0U;
  inbound_parse_errors_ = 0U;
  command_rejections_ = 0U;
  packet_count_by_type_.fill(0U);
  packet_bytes_by_type_.fill(0U);
  total_packets_sent_ = 0U;
  total_packet_bytes_ = 0U;
  peak_active_sessions_ = 0U;
  peak_tracked_players_ = 0U;
  peak_pending_inputs_ = 0U;
  peak_pending_snapshot_events_ = 0U;
  peak_active_treasure_spawns_ = 0U;
  peak_estimated_state_bytes_ = 0U;
  active_players_sum_ = 0U;
  active_players_samples_ = 0U;
  last_active_players_ = 0U;

  return report;
}

RuntimeTelemetryConfig RuntimeTelemetry::sanitize_config(RuntimeTelemetryConfig config) {
  if (config.report_interval_ticks == 0U) {
    config.report_interval_ticks = 1U;
  }
  if (config.history_size_ticks == 0U) {
    config.history_size_ticks = config.report_interval_ticks;
  }
  if (config.history_size_ticks < static_cast<std::size_t>(config.report_interval_ticks)) {
    config.history_size_ticks = config.report_interval_ticks;
  }
  if (!std::isfinite(config.tick_lag_tolerance_ms) || config.tick_lag_tolerance_ms < 0.0) {
    config.tick_lag_tolerance_ms = 0.5;
  }
  config.alerts.max_tick_lag_rate =
      sanitize_ratio(config.alerts.max_tick_lag_rate, 1.0);
  config.alerts.max_packet_drop_rate =
      sanitize_ratio(config.alerts.max_packet_drop_rate, 1.0);
  config.alerts.max_parse_error_rate =
      sanitize_ratio(config.alerts.max_parse_error_rate, 1.0);
  return config;
}

RuntimeTelemetryReport RuntimeTelemetry::build_report() const {
  RuntimeTelemetryReport report{};

  if (!tick_durations_ns_.empty()) {
    const uint64_t sum_ns =
        std::accumulate(tick_durations_ns_.begin(), tick_durations_ns_.end(), uint64_t{0U});
    report.tick_avg_ms = ns_to_ms(sum_ns) / static_cast<double>(tick_durations_ns_.size());

    std::vector<int64_t> sorted_durations = tick_durations_ns_;
    std::sort(sorted_durations.begin(), sorted_durations.end());
    const auto p95_idx = percentile_index(sorted_durations.size(), 0.95);
    report.tick_p95_ms = ns_to_ms(sorted_durations[p95_idx]);
    report.tick_max_ms = ns_to_ms(sorted_durations.back());
  }

  const double tick_count = static_cast<double>(std::max<std::size_t>(ticks_since_report_, 1U));
  for (std::size_t i = 0; i < kRuntimeTickPhaseCount; ++i) {
    report.phase_avg_ms[i] = ns_to_ms(phase_total_ns_[i]) / tick_count;
    report.phase_max_ms[i] = ns_to_ms(phase_max_ns_[i]);
  }

  report.tick_lag_count = tick_lag_count_;
  report.tick_lag_rate = static_cast<double>(tick_lag_count_) / tick_count;
  report.tick_budget_ms = ns_to_ms(tick_budget_ns_);
  report.max_tick_overrun_ms = ns_to_ms(max_tick_overrun_ns_);

  report.packet_count_by_type = packet_count_by_type_;
  report.packet_bytes_by_type = packet_bytes_by_type_;
  report.total_packets_sent = total_packets_sent_;
  report.total_packet_bytes = total_packet_bytes_;

  report.inbound_packets_received = inbound_packets_received_;
  report.inbound_packets_dropped = inbound_packets_dropped_;
  report.inbound_parse_errors = inbound_parse_errors_;
  report.command_rejections = command_rejections_;
  if (inbound_packets_received_ > 0U) {
    const double inbound_count = static_cast<double>(inbound_packets_received_);
    report.inbound_packet_drop_rate =
        static_cast<double>(inbound_packets_dropped_) / inbound_count;
    report.inbound_parse_error_rate =
        static_cast<double>(inbound_parse_errors_) / inbound_count;
  }

  report.peak_active_sessions = peak_active_sessions_;
  report.peak_tracked_players = peak_tracked_players_;
  report.peak_pending_inputs = peak_pending_inputs_;
  report.peak_pending_snapshot_events = peak_pending_snapshot_events_;
  report.peak_active_treasure_spawns = peak_active_treasure_spawns_;
  report.peak_estimated_state_bytes = peak_estimated_state_bytes_;
  report.last_active_players = last_active_players_;
  if (active_players_samples_ > 0U) {
    report.avg_active_players =
        static_cast<double>(active_players_sum_) / static_cast<double>(active_players_samples_);
  }

  if (report.tick_lag_rate > config_.alerts.max_tick_lag_rate) {
    report.alerts.push_back("tick_lag_rate_exceeded");
  }
  if (report.inbound_packet_drop_rate > config_.alerts.max_packet_drop_rate) {
    report.alerts.push_back("packet_drop_rate_exceeded");
  }
  if (report.inbound_parse_error_rate > config_.alerts.max_parse_error_rate) {
    report.alerts.push_back("parse_error_rate_exceeded");
  }
  if (config_.alerts.min_active_players > 0U &&
      report.avg_active_players <
          static_cast<double>(config_.alerts.min_active_players)) {
    report.alerts.push_back("active_players_below_min");
  }

  return report;
}

std::size_t RuntimeTelemetry::phase_index(RuntimeTickPhase phase) {
  const std::size_t index = static_cast<std::size_t>(phase);
  if (index >= kRuntimeTickPhaseCount) {
    return 0U;
  }
  return index;
}

std::size_t RuntimeTelemetry::message_type_index(devy::net::MessageType type) {
  const std::size_t index = static_cast<std::size_t>(type);
  if (index >= kRuntimeMessageTypeCount) {
    return static_cast<std::size_t>(devy::net::MessageType::Invalid);
  }
  return index;
}

std::size_t RuntimeTelemetry::estimate_state_bytes(const RuntimeResourceSnapshot& resources) {
  constexpr std::size_t kBytesPerSession = 256U;
  constexpr std::size_t kBytesPerTrackedPlayer = 160U;
  constexpr std::size_t kBytesPerPendingInput = 64U;
  constexpr std::size_t kBytesPerPendingSnapshotEvent = 128U;
  constexpr std::size_t kBytesPerActiveTreasureSpawn = 96U;

  return resources.active_sessions * kBytesPerSession +
         resources.tracked_players * kBytesPerTrackedPlayer +
         resources.pending_inputs * kBytesPerPendingInput +
         resources.pending_snapshot_events * kBytesPerPendingSnapshotEvent +
         resources.active_treasure_spawns * kBytesPerActiveTreasureSpawn;
}

const char* to_string(RuntimeTickPhase phase) {
  switch (phase) {
  case RuntimeTickPhase::Movement:
    return "movement";
  case RuntimeTickPhase::Combat:
    return "combat";
  case RuntimeTickPhase::Inventory:
    return "inventory";
  case RuntimeTickPhase::MatchLifecycle:
    return "match_lifecycle";
  case RuntimeTickPhase::Broadcast:
    return "broadcast";
  case RuntimeTickPhase::SnapshotBuild:
    return "snapshot_build";
  case RuntimeTickPhase::SnapshotSend:
    return "snapshot_send";
  case RuntimeTickPhase::Count:
    return "count";
  default:
    return "unknown";
  }
}

std::string format_runtime_telemetry_report(const RuntimeTelemetryReport& report) {
  std::size_t top_message_index = 0U;
  uint64_t top_message_bytes = 0U;
  for (std::size_t i = 0; i < report.packet_bytes_by_type.size(); ++i) {
    if (report.packet_bytes_by_type[i] > top_message_bytes) {
      top_message_bytes = report.packet_bytes_by_type[i];
      top_message_index = i;
    }
  }

  auto top_message_type = static_cast<devy::net::MessageType>(top_message_index);

  std::ostringstream out{};
  out << "Runtime profile tick=" << report.tick << " window_ticks=" << report.ticks_in_window
      << " tick_ms(avg/p95/max)=" << format_ms(report.tick_avg_ms) << "/"
      << format_ms(report.tick_p95_ms) << "/" << format_ms(report.tick_max_ms) << " phases_ms{";

  for (std::size_t i = 0; i < kRuntimeTickPhaseCount; ++i) {
    if (i > 0U) {
      out << ", ";
    }
    const auto phase = static_cast<RuntimeTickPhase>(i);
    out << to_string(phase) << "=" << format_ms(report.phase_avg_ms[i]) << "/"
        << format_ms(report.phase_max_ms[i]);
  }

  out << "} net{packets=" << report.total_packets_sent << ", bytes=" << report.total_packet_bytes
      << ", top=" << devy::net::to_string(top_message_type) << ":" << top_message_bytes
      << "} diag{players(avg/last)=" << format_ms(report.avg_active_players) << "/"
      << report.last_active_players << ", tick_lag=" << report.tick_lag_count << "/"
      << report.ticks_in_window << "(" << format_percent(report.tick_lag_rate)
      << "%), max_overrun_ms=" << format_ms(report.max_tick_overrun_ms)
      << ", inbound(received/dropped/parse/rejected)=" << report.inbound_packets_received << "/"
      << report.inbound_packets_dropped << "/" << report.inbound_parse_errors << "/"
      << report.command_rejections << ", drop_rate=" << format_percent(report.inbound_packet_drop_rate)
      << "%, parse_error_rate=" << format_percent(report.inbound_parse_error_rate) << "%}"
      << " peaks{sessions=" << report.peak_active_sessions
      << ", players=" << report.peak_tracked_players
      << ", pending_inputs=" << report.peak_pending_inputs
      << ", pending_events=" << report.peak_pending_snapshot_events
      << ", treasure_spawns=" << report.peak_active_treasure_spawns
      << ", est_state_bytes=" << report.peak_estimated_state_bytes << "}"
      << " alerts=";
  if (report.alerts.empty()) {
    out << "none";
  } else {
    for (std::size_t i = 0; i < report.alerts.size(); ++i) {
      if (i > 0U) {
        out << ",";
      }
      out << report.alerts[i];
    }
  }
  return out.str();
}

std::string format_runtime_diagnostics_json(const RuntimeTelemetryReport& report) {
  nlohmann::json diagnostics = {
      {"kind", "runtime_diagnostics_v1"},
      {"tick_index", report.tick},
      {"window_ticks", report.ticks_in_window},
      {"tick", {{"avg_ms", report.tick_avg_ms},
                {"p95_ms", report.tick_p95_ms},
                {"max_ms", report.tick_max_ms},
                {"budget_ms", report.tick_budget_ms},
                {"lag_count", report.tick_lag_count},
                {"lag_rate", report.tick_lag_rate},
                {"max_overrun_ms", report.max_tick_overrun_ms}}},
      {"inbound", {{"received", report.inbound_packets_received},
                   {"dropped", report.inbound_packets_dropped},
                   {"drop_rate", report.inbound_packet_drop_rate},
                   {"parse_errors", report.inbound_parse_errors},
                   {"parse_error_rate", report.inbound_parse_error_rate},
                   {"command_rejections", report.command_rejections}}},
      {"players", {{"avg_active", report.avg_active_players},
                   {"last_active", report.last_active_players},
                   {"peak_active", report.peak_active_sessions}}},
      {"alerts", report.alerts}};
  return "Runtime diagnostics json=" + diagnostics.dump();
}

} // namespace devy::server
