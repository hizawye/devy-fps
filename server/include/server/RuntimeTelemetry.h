#pragma once

#include "shared/net/Protocol.h"

#include <array>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace devy::server {

enum class RuntimeTickPhase : uint8_t {
  Movement = 0,
  Combat,
  Inventory,
  MatchLifecycle,
  Broadcast,
  SnapshotBuild,
  SnapshotSend,
  Count
};

inline constexpr std::size_t kRuntimeTickPhaseCount =
    static_cast<std::size_t>(RuntimeTickPhase::Count);
inline constexpr std::size_t kRuntimeMessageTypeCount =
    static_cast<std::size_t>(devy::net::MessageType::TreasurePickup) + 1U;

struct RuntimeTelemetryConfig {
  bool enabled{false};
  uint32_t report_interval_ticks{300};
  std::size_t history_size_ticks{600};
  double tick_lag_tolerance_ms{0.5};
  struct Alerts {
    double max_tick_lag_rate{1.0};
    double max_packet_drop_rate{1.0};
    double max_parse_error_rate{1.0};
    std::size_t min_active_players{0};
  } alerts{};
};

struct RuntimeResourceSnapshot {
  std::size_t active_sessions{0};
  std::size_t tracked_players{0};
  std::size_t pending_inputs{0};
  std::size_t pending_snapshot_events{0};
  std::size_t active_treasure_spawns{0};
};

struct RuntimeTelemetryReport {
  uint64_t tick{0};
  std::size_t ticks_in_window{0};
  double tick_avg_ms{0.0};
  double tick_p95_ms{0.0};
  double tick_max_ms{0.0};
  uint64_t tick_lag_count{0};
  double tick_lag_rate{0.0};
  double tick_budget_ms{0.0};
  double max_tick_overrun_ms{0.0};

  std::array<double, kRuntimeTickPhaseCount> phase_avg_ms{};
  std::array<double, kRuntimeTickPhaseCount> phase_max_ms{};

  std::array<uint64_t, kRuntimeMessageTypeCount> packet_count_by_type{};
  std::array<uint64_t, kRuntimeMessageTypeCount> packet_bytes_by_type{};
  uint64_t total_packets_sent{0};
  uint64_t total_packet_bytes{0};
  uint64_t inbound_packets_received{0};
  uint64_t inbound_packets_dropped{0};
  uint64_t inbound_parse_errors{0};
  uint64_t command_rejections{0};
  double inbound_packet_drop_rate{0.0};
  double inbound_parse_error_rate{0.0};

  std::size_t peak_active_sessions{0};
  std::size_t peak_tracked_players{0};
  std::size_t peak_pending_inputs{0};
  std::size_t peak_pending_snapshot_events{0};
  std::size_t peak_active_treasure_spawns{0};
  std::size_t peak_estimated_state_bytes{0};
  double avg_active_players{0.0};
  std::size_t last_active_players{0};

  std::vector<std::string> alerts{};
};

class RuntimeTelemetry {
public:
  explicit RuntimeTelemetry(RuntimeTelemetryConfig config = {});

  void reset();
  void set_tick_budget(std::chrono::nanoseconds tick_budget);
  void begin_tick(uint64_t tick, std::chrono::steady_clock::time_point started_at);
  void note_phase(RuntimeTickPhase phase, std::chrono::nanoseconds duration);
  void note_resources(const RuntimeResourceSnapshot& resources);
  void note_inbound_packet_received();
  void note_inbound_packet_drop(bool parse_error);
  void note_command_rejected();
  void note_outbound_packet(devy::net::MessageType type, std::size_t payload_size_bytes);
  [[nodiscard]] std::optional<RuntimeTelemetryReport>
  end_tick(std::chrono::steady_clock::time_point finished_at);

private:
  [[nodiscard]] static RuntimeTelemetryConfig sanitize_config(RuntimeTelemetryConfig config);
  [[nodiscard]] RuntimeTelemetryReport build_report() const;
  [[nodiscard]] static std::size_t phase_index(RuntimeTickPhase phase);
  [[nodiscard]] static std::size_t message_type_index(devy::net::MessageType type);
  [[nodiscard]] static std::size_t estimate_state_bytes(const RuntimeResourceSnapshot& resources);

  RuntimeTelemetryConfig config_{};
  bool tick_active_{false};
  uint64_t current_tick_{0};
  std::chrono::steady_clock::time_point tick_started_at_{};
  std::size_t ticks_since_report_{0};
  int64_t tick_budget_ns_{0};
  int64_t tick_lag_tolerance_ns_{0};
  uint64_t tick_lag_count_{0};
  int64_t max_tick_overrun_ns_{0};

  std::vector<int64_t> tick_durations_ns_{};
  std::array<int64_t, kRuntimeTickPhaseCount> phase_total_ns_{};
  std::array<int64_t, kRuntimeTickPhaseCount> phase_max_ns_{};

  uint64_t inbound_packets_received_{0};
  uint64_t inbound_packets_dropped_{0};
  uint64_t inbound_parse_errors_{0};
  uint64_t command_rejections_{0};
  std::array<uint64_t, kRuntimeMessageTypeCount> packet_count_by_type_{};
  std::array<uint64_t, kRuntimeMessageTypeCount> packet_bytes_by_type_{};
  uint64_t total_packets_sent_{0};
  uint64_t total_packet_bytes_{0};

  std::size_t peak_active_sessions_{0};
  std::size_t peak_tracked_players_{0};
  std::size_t peak_pending_inputs_{0};
  std::size_t peak_pending_snapshot_events_{0};
  std::size_t peak_active_treasure_spawns_{0};
  std::size_t peak_estimated_state_bytes_{0};
  uint64_t active_players_sum_{0};
  std::size_t active_players_samples_{0};
  std::size_t last_active_players_{0};
};

const char* to_string(RuntimeTickPhase phase);
std::string format_runtime_telemetry_report(const RuntimeTelemetryReport& report);
std::string format_runtime_diagnostics_json(const RuntimeTelemetryReport& report);

} // namespace devy::server
