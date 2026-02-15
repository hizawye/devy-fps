#include "server/AuthoritativeLoop.h"
#include "server/BlockInteraction.h"
#include "server/CombatEvents.h"
#include "server/CombatSimulation.h"
#include "server/InventoryLootSimulation.h"
#include "server/MatchStatePayload.h"
#include "server/MatchLifecycleSimulation.h"
#include "server/MovementSimulation.h"
#include "server/RuntimeTelemetry.h"
#include "server/ServerConfigValidation.h"
#include "server/SessionManager.h"
#include "server/WorldReplication.h"
#include "shared/Config.h"
#include "shared/Log.h"
#include "shared/game/Treasure.h"
#include "shared/game/Weapons.h"
#include "shared/net/Protocol.h"
#include "shared/voxel/World.h"

#include <enet/enet.h>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <csignal>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <limits>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <thread>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

namespace {
std::atomic<bool> running{true};

void handle_signal(int) { running = false; }

std::string resolve_path(const std::string& path) {
  if (std::filesystem::exists(path)) {
    return path;
  }
  auto alt = std::filesystem::path("..") / path;
  if (std::filesystem::exists(alt)) {
    return alt.string();
  }
  return path;
}

constexpr std::string_view kRuntimeDiagnosticsPrefix = "Runtime diagnostics json=";

bool write_atomic_text_file(const std::string& output_path, const std::string& body,
                            std::string* error) {
  if (output_path.empty()) {
    if (error != nullptr) {
      *error = "health output path is empty";
    }
    return false;
  }

  const std::filesystem::path path(output_path);
  const auto parent = path.parent_path();
  if (!parent.empty()) {
    std::error_code mkdir_error{};
    std::filesystem::create_directories(parent, mkdir_error);
    if (mkdir_error) {
      if (error != nullptr) {
        *error = "failed to create health output directory `" + parent.string() +
                 "`: " + mkdir_error.message();
      }
      return false;
    }
  }

  const std::filesystem::path tmp_path = path.string() + ".tmp";
  std::ofstream out(tmp_path, std::ios::binary | std::ios::trunc);
  if (!out.is_open()) {
    if (error != nullptr) {
      *error = "failed to open temporary health output file `" + tmp_path.string() + "`";
    }
    return false;
  }
  out << body;
  out << '\n';
  out.flush();
  if (!out.good()) {
    if (error != nullptr) {
      *error = "failed to write temporary health output file `" + tmp_path.string() + "`";
    }
    out.close();
    std::error_code remove_error{};
    std::filesystem::remove(tmp_path, remove_error);
    return false;
  }
  out.close();

  std::error_code rename_error{};
  std::filesystem::rename(tmp_path, path, rename_error);
  if (rename_error) {
    std::error_code remove_error{};
    std::filesystem::remove(path, remove_error);
    rename_error.clear();
    std::filesystem::rename(tmp_path, path, rename_error);
    if (rename_error) {
      if (error != nullptr) {
        *error = "failed to atomically publish health output file `" + path.string() +
                 "`: " + rename_error.message();
      }
      std::error_code cleanup_error{};
      std::filesystem::remove(tmp_path, cleanup_error);
      return false;
    }
  }

  return true;
}

std::optional<std::string> diagnostics_json_payload(const std::string& diagnostics_line) {
  if (diagnostics_line.rfind(kRuntimeDiagnosticsPrefix, 0U) != 0U) {
    return std::nullopt;
  }
  return diagnostics_line.substr(kRuntimeDiagnosticsPrefix.size());
}

std::uintptr_t peer_token(const ENetPeer* peer) { return reinterpret_cast<std::uintptr_t>(peer); }

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
  }
  return std::nullopt;
}

std::optional<uint64_t> json_to_u64(const nlohmann::json& value) {
  if (value.is_number_unsigned()) {
    return value.get<uint64_t>();
  }

  if (value.is_number_integer()) {
    const int64_t raw = value.get<int64_t>();
    if (raw < 0) {
      return std::nullopt;
    }
    return static_cast<uint64_t>(raw);
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

std::optional<int32_t> json_to_i32(const nlohmann::json& value) {
  if (value.is_number_integer()) {
    const int64_t raw = value.get<int64_t>();
    if (raw < static_cast<int64_t>(std::numeric_limits<int32_t>::min()) ||
        raw > static_cast<int64_t>(std::numeric_limits<int32_t>::max())) {
      return std::nullopt;
    }
    return static_cast<int32_t>(raw);
  }

  if (value.is_number_unsigned()) {
    const uint64_t raw = value.get<uint64_t>();
    if (raw > static_cast<uint64_t>(std::numeric_limits<int32_t>::max())) {
      return std::nullopt;
    }
    return static_cast<int32_t>(raw);
  }
  return std::nullopt;
}

std::optional<uint8_t> json_to_u8(const nlohmann::json& value) {
  const auto raw = json_to_u32(value);
  if (!raw.has_value() ||
      raw.value() > static_cast<uint32_t>(std::numeric_limits<uint8_t>::max())) {
    return std::nullopt;
  }
  return static_cast<uint8_t>(raw.value());
}

void send_packet(ENetPeer* peer, const devy::net::Packet& packet,
                 devy::server::RuntimeTelemetry* telemetry) {
  std::string payload = devy::net::serialize(packet);
  if (telemetry != nullptr) {
    telemetry->note_outbound_packet(packet.type, payload.size());
  }
  ENetPacket* out = enet_packet_create(payload.data(), payload.size(), ENET_PACKET_FLAG_RELIABLE);
  enet_peer_send(peer, 0, out);
}

devy::net::Packet inventory_update_packet(const devy::server::InventorySummary& summary) {
  return {devy::net::MessageType::InventoryUpdate,
          {{"player_id", summary.player_id},
           {"coins", summary.total_value},
           {"total_weight", summary.total_weight},
           {"item_count", summary.item_count},
           {"last_pickup_seq", summary.last_pickup_seq}},
          devy::net::kProtocolVersion};
}

nlohmann::json to_snapshot_event(uint64_t tick, const devy::server::TreasureSpawnInstance& spawn) {
  return {{"type", "treasure_spawned"},
          {"tick", tick},
          {"spawn_id", spawn.spawn_id},
          {"treasure_id", spawn.treasure_id},
          {"value", spawn.value},
          {"weight", spawn.weight},
          {"position", {{"x", spawn.position_x}, {"y", spawn.position_y}}},
          {"source", devy::server::to_string(spawn.source)}};
}

nlohmann::json to_snapshot_event(uint64_t tick,
                                 const devy::server::TreasurePickupResolution& pickup) {
  return {{"type", "treasure_pickup"},           {"tick", tick},
          {"player_id", pickup.player_id},       {"pickup_seq", pickup.pickup_seq},
          {"spawn_id", pickup.spawn_id},         {"status", devy::server::to_string(pickup.status)},
          {"treasure_id", pickup.treasure_id},   {"total_value", pickup.total_value},
          {"total_weight", pickup.total_weight}, {"item_count", pickup.item_count}};
}

nlohmann::json spawns_to_json(const std::vector<devy::server::TreasureSpawnInstance>& spawns) {
  nlohmann::json out = nlohmann::json::array();
  out.get_ref<nlohmann::json::array_t&>().reserve(spawns.size());
  for (const auto& spawn : spawns) {
    out.push_back({{"spawn_id", spawn.spawn_id},
                   {"treasure_id", spawn.treasure_id},
                   {"value", spawn.value},
                   {"weight", spawn.weight},
                   {"position", {{"x", spawn.position_x}, {"y", spawn.position_y}}},
                   {"source", devy::server::to_string(spawn.source)},
                   {"spawned_tick", spawn.spawned_tick}});
  }
  return out;
}

nlohmann::json chunk_revisions_to_json(
    const std::vector<devy::server::ChunkRevisionEntry>& entries) {
  nlohmann::json out = nlohmann::json::array();
  out.get_ref<nlohmann::json::array_t&>().reserve(entries.size());
  for (const auto& entry : entries) {
    out.push_back(
        {{"x", entry.coord.x}, {"y", entry.coord.y}, {"z", entry.coord.z}, {"revision", entry.revision}});
  }
  return out;
}

std::string join_error_message(devy::server::JoinError error) {
  switch (error) {
  case devy::server::JoinError::InvalidPlayerName:
    return "Invalid player name.";
  case devy::server::JoinError::NameInUse:
    return "Player name is already in use.";
  case devy::server::JoinError::ServerFull:
    return "Server is full.";
  case devy::server::JoinError::None:
    return "Join rejected.";
  default:
    return "Join rejected.";
  }
}
} // namespace

int main(int argc, char** argv) {
  running = true;
  std::signal(SIGINT, handle_signal);
  std::signal(SIGTERM, handle_signal);

  std::string config_path = "config/server.json";
  std::string health_file_path{};
  int smoke_seconds = 0;
  for (int i = 1; i < argc; ++i) {
    const std::string arg = argv[i];
    if (arg == "--smoke-seconds") {
      if (i + 1 >= argc) {
        devy::log::write(devy::log::Level::Error, "Missing value for --smoke-seconds.");
        return 1;
      }
      try {
        smoke_seconds = std::stoi(argv[++i]);
      } catch (const std::invalid_argument&) {
        devy::log::write(devy::log::Level::Error, "Invalid integer for --smoke-seconds.");
        return 1;
      } catch (const std::out_of_range&) {
        devy::log::write(devy::log::Level::Error, "Out-of-range integer for --smoke-seconds.");
        return 1;
      }
      continue;
    }
    if (arg == "--health-file") {
      if (i + 1 >= argc) {
        devy::log::write(devy::log::Level::Error, "Missing value for --health-file.");
        return 1;
      }
      health_file_path = argv[++i];
      continue;
    }
    config_path = arg;
  }
  config_path = resolve_path(config_path);

  const auto loaded_config = devy::config::try_load_json(config_path);
  if (!loaded_config.has_value()) {
    devy::log::write(devy::log::Level::Error, "Configuration load failed for " + config_path + ".");
    return 1;
  }
  const auto& config = loaded_config.value();
  const auto config_errors = devy::server::validate_server_config(config);
  if (!config_errors.empty()) {
    devy::log::write(devy::log::Level::Error,
                     "Configuration validation failed for " + config_path + ":");
    for (const auto& error : config_errors) {
      devy::log::write(devy::log::Level::Error, "  - " + error);
    }
    return 1;
  }

  int max_players = config.value("max_players", 64);
  int port = config.value("port", 7777);
  if (max_players <= 0) {
    devy::log::write(devy::log::Level::Warn, "Invalid max_players; using 64.");
    max_players = 64;
  }
  int heartbeat_timeout_ms = 10000;
  if (config.contains("session") && config["session"].is_object()) {
    heartbeat_timeout_ms = config["session"].value("heartbeat_timeout_ms", heartbeat_timeout_ms);
  }
  if (heartbeat_timeout_ms <= 0) {
    devy::log::write(devy::log::Level::Warn, "Invalid heartbeat timeout; using 10000 ms.");
    heartbeat_timeout_ms = 10000;
  }

  int tick_rate_hz = 30;
  int snapshot_interval_ticks = 2;
  int input_queue_capacity = 2048;
  double movement_speed_units_per_second = 6.0;
  bool runtime_profiling_enabled = true;
  int runtime_profiling_report_interval_ticks = 300;
  int runtime_profiling_history_size_ticks = 600;
  double runtime_profiling_tick_lag_tolerance_ms = 0.5;
  double runtime_profiling_alert_max_tick_lag_rate = 1.0;
  double runtime_profiling_alert_max_packet_drop_rate = 1.0;
  double runtime_profiling_alert_max_parse_error_rate = 1.0;
  int runtime_profiling_alert_min_active_players = 0;
  bool runtime_match_state_broadcast_on_snapshot = false;
  bool runtime_snapshot_include_match_scoreboard = false;
  if (config.contains("runtime") && config["runtime"].is_object()) {
    tick_rate_hz = config["runtime"].value("tick_rate_hz", tick_rate_hz);
    snapshot_interval_ticks =
        config["runtime"].value("snapshot_interval_ticks", snapshot_interval_ticks);
    input_queue_capacity = config["runtime"].value("input_queue_capacity", input_queue_capacity);
    movement_speed_units_per_second =
        config["runtime"].value("movement_speed_units_per_second", movement_speed_units_per_second);
    runtime_match_state_broadcast_on_snapshot = config["runtime"].value(
        "match_state_broadcast_on_snapshot", runtime_match_state_broadcast_on_snapshot);
    runtime_snapshot_include_match_scoreboard = config["runtime"].value(
        "snapshot_include_match_scoreboard", runtime_snapshot_include_match_scoreboard);
    if (config["runtime"].contains("profiling") && config["runtime"]["profiling"].is_object()) {
      const auto& profiling = config["runtime"]["profiling"];
      runtime_profiling_enabled = profiling.value("enabled", runtime_profiling_enabled);
      runtime_profiling_report_interval_ticks =
          profiling.value("report_interval_ticks", runtime_profiling_report_interval_ticks);
      runtime_profiling_history_size_ticks =
          profiling.value("history_size_ticks", runtime_profiling_history_size_ticks);
      runtime_profiling_tick_lag_tolerance_ms =
          profiling.value("tick_lag_tolerance_ms", runtime_profiling_tick_lag_tolerance_ms);
      if (profiling.contains("alerts") && profiling["alerts"].is_object()) {
        const auto& alerts = profiling["alerts"];
        runtime_profiling_alert_max_tick_lag_rate = alerts.value(
            "max_tick_lag_rate", runtime_profiling_alert_max_tick_lag_rate);
        runtime_profiling_alert_max_packet_drop_rate = alerts.value(
            "max_packet_drop_rate", runtime_profiling_alert_max_packet_drop_rate);
        runtime_profiling_alert_max_parse_error_rate = alerts.value(
            "max_parse_error_rate", runtime_profiling_alert_max_parse_error_rate);
        runtime_profiling_alert_min_active_players = alerts.value(
            "min_active_players", runtime_profiling_alert_min_active_players);
      }
    }
  }
  if (tick_rate_hz <= 0) {
    devy::log::write(devy::log::Level::Warn, "Invalid tick rate; using 30 Hz.");
    tick_rate_hz = 30;
  }
  if (snapshot_interval_ticks <= 0) {
    devy::log::write(devy::log::Level::Warn, "Invalid snapshot interval; using 2 ticks.");
    snapshot_interval_ticks = 2;
  }
  if (input_queue_capacity <= 0) {
    devy::log::write(devy::log::Level::Warn, "Invalid input queue capacity; using 2048.");
    input_queue_capacity = 2048;
  }
  if (!std::isfinite(movement_speed_units_per_second) || movement_speed_units_per_second <= 0.0) {
    devy::log::write(devy::log::Level::Warn, "Invalid movement speed; using 6.0 units/sec.");
    movement_speed_units_per_second = 6.0;
  }
  if (runtime_profiling_report_interval_ticks <= 0) {
    devy::log::write(devy::log::Level::Warn,
                     "Invalid runtime profiling report interval; using 300 ticks.");
    runtime_profiling_report_interval_ticks = 300;
  }
  if (runtime_profiling_history_size_ticks <= 0) {
    devy::log::write(devy::log::Level::Warn,
                     "Invalid runtime profiling history size; using report interval ticks.");
    runtime_profiling_history_size_ticks = runtime_profiling_report_interval_ticks;
  }
  if (runtime_profiling_history_size_ticks < runtime_profiling_report_interval_ticks) {
    devy::log::write(devy::log::Level::Warn,
                     "Runtime profiling history is smaller than report interval; clamping.");
    runtime_profiling_history_size_ticks = runtime_profiling_report_interval_ticks;
  }
  if (!std::isfinite(runtime_profiling_tick_lag_tolerance_ms) ||
      runtime_profiling_tick_lag_tolerance_ms < 0.0) {
    devy::log::write(devy::log::Level::Warn,
                     "Invalid runtime profiling tick lag tolerance; using 0.5 ms.");
    runtime_profiling_tick_lag_tolerance_ms = 0.5;
  }
  if (!std::isfinite(runtime_profiling_alert_max_tick_lag_rate) ||
      runtime_profiling_alert_max_tick_lag_rate < 0.0 ||
      runtime_profiling_alert_max_tick_lag_rate > 1.0) {
    devy::log::write(devy::log::Level::Warn,
                     "Invalid runtime profiling alert max_tick_lag_rate; clamping to [0,1].");
    runtime_profiling_alert_max_tick_lag_rate =
        std::clamp(runtime_profiling_alert_max_tick_lag_rate, 0.0, 1.0);
  }
  if (!std::isfinite(runtime_profiling_alert_max_packet_drop_rate) ||
      runtime_profiling_alert_max_packet_drop_rate < 0.0 ||
      runtime_profiling_alert_max_packet_drop_rate > 1.0) {
    devy::log::write(devy::log::Level::Warn,
                     "Invalid runtime profiling alert max_packet_drop_rate; clamping to [0,1].");
    runtime_profiling_alert_max_packet_drop_rate =
        std::clamp(runtime_profiling_alert_max_packet_drop_rate, 0.0, 1.0);
  }
  if (!std::isfinite(runtime_profiling_alert_max_parse_error_rate) ||
      runtime_profiling_alert_max_parse_error_rate < 0.0 ||
      runtime_profiling_alert_max_parse_error_rate > 1.0) {
    devy::log::write(devy::log::Level::Warn,
                     "Invalid runtime profiling alert max_parse_error_rate; clamping to [0,1].");
    runtime_profiling_alert_max_parse_error_rate =
        std::clamp(runtime_profiling_alert_max_parse_error_rate, 0.0, 1.0);
  }
  if (runtime_profiling_alert_min_active_players < 0) {
    devy::log::write(
        devy::log::Level::Warn,
        "Invalid runtime profiling alert min_active_players; using zero (disabled).");
    runtime_profiling_alert_min_active_players = 0;
  }

  int combat_starting_health = 100;
  double combat_hitscan_range_units = 128.0;
  double combat_projectile_range_units = 96.0;
  double combat_projectile_speed_units_per_second = 40.0;
  double combat_hit_radius_units = 0.75;
  if (config.contains("combat") && config["combat"].is_object()) {
    combat_starting_health = config["combat"].value("starting_health", combat_starting_health);
    combat_hitscan_range_units =
        config["combat"].value("hitscan_range_units", combat_hitscan_range_units);
    combat_projectile_range_units =
        config["combat"].value("projectile_range_units", combat_projectile_range_units);
    combat_projectile_speed_units_per_second = config["combat"].value(
        "projectile_speed_units_per_second", combat_projectile_speed_units_per_second);
    combat_hit_radius_units = config["combat"].value("hit_radius_units", combat_hit_radius_units);
  }
  if (combat_starting_health <= 0) {
    devy::log::write(devy::log::Level::Warn, "Invalid combat.starting_health; using 100.");
    combat_starting_health = 100;
  }
  if (!std::isfinite(combat_hitscan_range_units) || combat_hitscan_range_units <= 0.0) {
    devy::log::write(devy::log::Level::Warn, "Invalid combat.hitscan_range_units; using 128.0.");
    combat_hitscan_range_units = 128.0;
  }
  if (!std::isfinite(combat_projectile_range_units) || combat_projectile_range_units <= 0.0) {
    devy::log::write(devy::log::Level::Warn, "Invalid combat.projectile_range_units; using 96.0.");
    combat_projectile_range_units = 96.0;
  }
  if (!std::isfinite(combat_projectile_speed_units_per_second) ||
      combat_projectile_speed_units_per_second <= 0.0) {
    devy::log::write(devy::log::Level::Warn,
                     "Invalid combat.projectile_speed_units_per_second; using 40.0.");
    combat_projectile_speed_units_per_second = 40.0;
  }
  if (!std::isfinite(combat_hit_radius_units) || combat_hit_radius_units <= 0.0) {
    devy::log::write(devy::log::Level::Warn, "Invalid combat.hit_radius_units; using 0.75.");
    combat_hit_radius_units = 0.75;
  }

  int inventory_spawn_interval_ticks = 90;
  int inventory_max_active_spawns = 64;
  int inventory_pickup_queue_capacity = 2048;
  double inventory_pickup_radius_units = 2.5;
  int inventory_max_items_per_player = 16;
  int inventory_max_weight_per_player = 40;
  double inventory_death_drop_spread_units = 0.75;
  if (config.contains("inventory") && config["inventory"].is_object()) {
    inventory_spawn_interval_ticks =
        config["inventory"].value("spawn_interval_ticks", inventory_spawn_interval_ticks);
    inventory_max_active_spawns =
        config["inventory"].value("max_active_spawns", inventory_max_active_spawns);
    inventory_pickup_queue_capacity =
        config["inventory"].value("pickup_queue_capacity", inventory_pickup_queue_capacity);
    inventory_pickup_radius_units =
        config["inventory"].value("pickup_radius_units", inventory_pickup_radius_units);
    inventory_max_items_per_player =
        config["inventory"].value("max_items_per_player", inventory_max_items_per_player);
    inventory_max_weight_per_player =
        config["inventory"].value("max_weight_per_player", inventory_max_weight_per_player);
    inventory_death_drop_spread_units =
        config["inventory"].value("death_drop_spread_units", inventory_death_drop_spread_units);
  }
  if (inventory_spawn_interval_ticks <= 0) {
    devy::log::write(devy::log::Level::Warn, "Invalid inventory.spawn_interval_ticks; using 90.");
    inventory_spawn_interval_ticks = 90;
  }
  if (inventory_max_active_spawns <= 0) {
    devy::log::write(devy::log::Level::Warn, "Invalid inventory.max_active_spawns; using 64.");
    inventory_max_active_spawns = 64;
  }
  if (inventory_pickup_queue_capacity <= 0) {
    devy::log::write(devy::log::Level::Warn,
                     "Invalid inventory.pickup_queue_capacity; using 2048.");
    inventory_pickup_queue_capacity = 2048;
  }
  if (!std::isfinite(inventory_pickup_radius_units) || inventory_pickup_radius_units <= 0.0) {
    devy::log::write(devy::log::Level::Warn, "Invalid inventory.pickup_radius_units; using 2.5.");
    inventory_pickup_radius_units = 2.5;
  }
  if (inventory_max_items_per_player <= 0) {
    devy::log::write(devy::log::Level::Warn, "Invalid inventory.max_items_per_player; using 16.");
    inventory_max_items_per_player = 16;
  }
  if (inventory_max_weight_per_player <= 0) {
    devy::log::write(devy::log::Level::Warn, "Invalid inventory.max_weight_per_player; using 40.");
    inventory_max_weight_per_player = 40;
  }
  if (!std::isfinite(inventory_death_drop_spread_units) ||
      inventory_death_drop_spread_units < 0.0) {
    devy::log::write(devy::log::Level::Warn,
                     "Invalid inventory.death_drop_spread_units; using 0.75.");
    inventory_death_drop_spread_units = 0.75;
  }

  const std::string loot_drop_string = config.value("loot_drop", std::string("all"));
  const devy::server::LootDropMode loot_drop_mode =
      devy::server::parse_loot_drop_mode(loot_drop_string);

  int match_time_minutes = config.value("match_time_minutes", 60);
  int match_duration_seconds = match_time_minutes * 60;
  int match_respawns_per_player = config.value("respawns", 2);
  int match_pre_match_seconds = 5;
  int match_respawn_delay_seconds = 3;
  int match_min_players_to_start = 1;
  if (config.contains("match") && config["match"].is_object()) {
    match_pre_match_seconds = config["match"].value("pre_match_seconds", match_pre_match_seconds);
    match_respawn_delay_seconds =
        config["match"].value("respawn_delay_seconds", match_respawn_delay_seconds);
    match_min_players_to_start =
        config["match"].value("min_players_to_start", match_min_players_to_start);
    match_respawns_per_player =
        config["match"].value("respawns_per_player", match_respawns_per_player);
    match_duration_seconds = config["match"].value("duration_seconds", match_duration_seconds);
  }
  if (match_duration_seconds <= 0) {
    devy::log::write(devy::log::Level::Warn, "Invalid match duration; using 3600 seconds.");
    match_duration_seconds = 3600;
  }
  if (match_respawns_per_player < 0) {
    devy::log::write(devy::log::Level::Warn, "Invalid respawn count; using 2.");
    match_respawns_per_player = 2;
  }
  if (match_pre_match_seconds < 0) {
    devy::log::write(devy::log::Level::Warn, "Invalid match pre-match seconds; using 5.");
    match_pre_match_seconds = 5;
  }
  if (match_respawn_delay_seconds <= 0) {
    devy::log::write(devy::log::Level::Warn, "Invalid match respawn delay; using 3 seconds.");
    match_respawn_delay_seconds = 3;
  }
  if (match_min_players_to_start <= 0) {
    devy::log::write(devy::log::Level::Warn, "Invalid match min_players_to_start; using 1.");
    match_min_players_to_start = 1;
  }

  const auto seconds_to_ticks = [tick_rate_hz](int seconds, uint32_t floor_ticks) {
    if (seconds <= 0 || tick_rate_hz <= 0) {
      return floor_ticks;
    }
    const int64_t raw_ticks = static_cast<int64_t>(seconds) * static_cast<int64_t>(tick_rate_hz);
    if (raw_ticks <= 0) {
      return floor_ticks;
    }
    if (raw_ticks > static_cast<int64_t>(std::numeric_limits<uint32_t>::max())) {
      return std::numeric_limits<uint32_t>::max();
    }
    return static_cast<uint32_t>(raw_ticks);
  };
  const uint32_t match_pre_match_ticks = seconds_to_ticks(match_pre_match_seconds, 0U);
  const uint32_t match_respawn_delay_ticks = seconds_to_ticks(match_respawn_delay_seconds, 1U);

  int map_chunks_x = 64;
  int map_chunks_z = 64;
  int map_world_height = 256;
  int map_draw_distance_chunks = 2;
  if (config.contains("map") && config["map"].is_object()) {
    map_chunks_x = config["map"].value("chunks_x", map_chunks_x);
    map_chunks_z = config["map"].value("chunks_z", map_chunks_z);
    map_world_height = config["map"].value("world_height", map_world_height);
    map_draw_distance_chunks =
        config["map"].value("draw_distance_chunks", map_draw_distance_chunks);
  }
  if (map_chunks_x <= 0) {
    devy::log::write(devy::log::Level::Warn, "Invalid map.chunks_x; using 64.");
    map_chunks_x = 64;
  }
  if (map_chunks_z <= 0) {
    devy::log::write(devy::log::Level::Warn, "Invalid map.chunks_z; using 64.");
    map_chunks_z = 64;
  }
  if (map_world_height <= 0) {
    devy::log::write(devy::log::Level::Warn, "Invalid map.world_height; using 256.");
    map_world_height = 256;
  }
  if (map_draw_distance_chunks < 0) {
    devy::log::write(devy::log::Level::Warn, "Invalid map.draw_distance_chunks; using 2.");
    map_draw_distance_chunks = 2;
  }
  int map_chunks_y = (map_world_height + devy::voxel::kChunkSize - 1) / devy::voxel::kChunkSize;
  if (map_chunks_y <= 0) {
    map_chunks_y = 1;
  }

  devy::voxel::World world{};
  world.generate(map_chunks_x, map_chunks_z, map_world_height);

  std::unordered_set<devy::voxel::BlockId> valid_block_ids{static_cast<devy::voxel::BlockId>(0U)};
  const auto blocks_config = devy::config::load_json(resolve_path("config/blocks.json"));
  if (blocks_config.contains("blocks") && blocks_config["blocks"].is_array()) {
    for (const auto& block_entry : blocks_config["blocks"]) {
      if (!block_entry.is_object() || !block_entry.contains("id")) {
        continue;
      }
      const auto block_id = json_to_u8(block_entry["id"]);
      if (block_id.has_value()) {
        valid_block_ids.insert(block_id.value());
      }
    }
  }
  if (valid_block_ids.size() <= 1U) {
    valid_block_ids.insert(static_cast<devy::voxel::BlockId>(1U));
    valid_block_ids.insert(static_cast<devy::voxel::BlockId>(2U));
    valid_block_ids.insert(static_cast<devy::voxel::BlockId>(3U));
    devy::log::write(devy::log::Level::Warn,
                     "Block config ids unavailable; defaulting to {0,1,2,3}.");
  }

  auto weapon_definitions = devy::game::load_weapons(resolve_path("config/weapons.json"));
  if (weapon_definitions.empty()) {
    devy::log::write(devy::log::Level::Warn,
                     "Weapon config unavailable or empty; combat fire requests will be rejected.");
  }
  auto treasure_definitions = devy::game::load_treasures(resolve_path("config/treasure.json"));
  if (treasure_definitions.empty()) {
    devy::log::write(devy::log::Level::Warn,
                     "Treasure config unavailable or empty; scheduled loot spawns are disabled.");
  }

  if (enet_initialize() != 0) {
    devy::log::write(devy::log::Level::Error, "ENet initialization failed.");
    return 1;
  }

  ENetAddress address{};
  address.host = ENET_HOST_ANY;
  address.port = static_cast<enet_uint16>(port);

  ENetHost* server =
      enet_host_create(&address, static_cast<size_t>(max_players), static_cast<size_t>(2), 0, 0);
  if (!server) {
    devy::log::write(devy::log::Level::Error, "Failed to create ENet server.");
    enet_deinitialize();
    return 1;
  }

  devy::log::write(devy::log::Level::Info, "Server started on port " + std::to_string(port) + ".");
  devy::log::write(devy::log::Level::Info,
                   "Session heartbeat timeout: " + std::to_string(heartbeat_timeout_ms) + " ms.");
  devy::log::write(devy::log::Level::Info,
                   "Server tick rate: " + std::to_string(tick_rate_hz) + " Hz.");
  devy::log::write(devy::log::Level::Info, "Snapshot interval: every " +
                                               std::to_string(snapshot_interval_ticks) +
                                               " tick(s).");
  devy::log::write(devy::log::Level::Info,
                   "Input queue capacity: " + std::to_string(input_queue_capacity) + " packets.");
  devy::log::write(devy::log::Level::Info,
                   "Movement speed: " + std::to_string(movement_speed_units_per_second) +
                       " units/sec.");
  devy::log::write(
      devy::log::Level::Info,
      std::string("Runtime profiling: ") + (runtime_profiling_enabled ? "enabled" : "disabled") +
          ", report_interval_ticks=" + std::to_string(runtime_profiling_report_interval_ticks) +
          ", history_size_ticks=" + std::to_string(runtime_profiling_history_size_ticks) +
          ", tick_lag_tolerance_ms=" + std::to_string(runtime_profiling_tick_lag_tolerance_ms) +
          ", alert_max_tick_lag_rate=" +
          std::to_string(runtime_profiling_alert_max_tick_lag_rate) +
          ", alert_max_packet_drop_rate=" +
          std::to_string(runtime_profiling_alert_max_packet_drop_rate) +
          ", alert_max_parse_error_rate=" +
          std::to_string(runtime_profiling_alert_max_parse_error_rate) +
          ", alert_min_active_players=" +
          std::to_string(runtime_profiling_alert_min_active_players) + ".");
  devy::log::write(devy::log::Level::Info,
                   std::string("Match state reliable broadcast on snapshot: ") +
                       (runtime_match_state_broadcast_on_snapshot ? "enabled" : "disabled") + ".");
  devy::log::write(devy::log::Level::Info,
                   std::string("Snapshot includes match scoreboard: ") +
                       (runtime_snapshot_include_match_scoreboard ? "enabled" : "disabled") + ".");
  devy::log::write(devy::log::Level::Info, "World replication interest radius: " +
                                               std::to_string(map_draw_distance_chunks) +
                                               " chunk(s).");
  devy::log::write(devy::log::Level::Info, "Block interaction valid block ids: " +
                                               std::to_string(valid_block_ids.size()) + ".");
  devy::log::write(devy::log::Level::Info, "Combat weapon definitions loaded: " +
                                               std::to_string(weapon_definitions.size()) + ".");
  devy::log::write(devy::log::Level::Info, "Treasure definitions loaded: " +
                                               std::to_string(treasure_definitions.size()) + ".");
  devy::log::write(
      devy::log::Level::Info,
      "Combat defaults: health=" + std::to_string(combat_starting_health) +
          ", hitscan_range=" + std::to_string(combat_hitscan_range_units) +
          ", projectile_range=" + std::to_string(combat_projectile_range_units) +
          ", projectile_speed=" + std::to_string(combat_projectile_speed_units_per_second) +
          ", hit_radius=" + std::to_string(combat_hit_radius_units) + ".");
  devy::log::write(
      devy::log::Level::Info,
      "Inventory defaults: spawn_interval_ticks=" + std::to_string(inventory_spawn_interval_ticks) +
          ", max_active_spawns=" + std::to_string(inventory_max_active_spawns) +
          ", pickup_radius=" + std::to_string(inventory_pickup_radius_units) +
          ", max_items_per_player=" + std::to_string(inventory_max_items_per_player) +
          ", max_weight_per_player=" + std::to_string(inventory_max_weight_per_player) +
          ", loot_drop=" + devy::server::to_string(loot_drop_mode) + ".");
  devy::log::write(
      devy::log::Level::Info,
      "Match lifecycle defaults: pre_match_ticks=" + std::to_string(match_pre_match_ticks) +
          ", duration_seconds=" + std::to_string(match_duration_seconds) +
          ", respawns_per_player=" + std::to_string(match_respawns_per_player) +
          ", respawn_delay_ticks=" + std::to_string(match_respawn_delay_ticks) +
          ", min_players_to_start=" + std::to_string(match_min_players_to_start) + ".");
  if (smoke_seconds > 0) {
    devy::log::write(devy::log::Level::Info,
                     "Smoke mode active for " + std::to_string(smoke_seconds) + " seconds.");
  }
  if (!health_file_path.empty()) {
    devy::log::write(devy::log::Level::Info,
                     "Health diagnostics file: " + health_file_path + ".");
  }

  devy::server::SessionManager session_manager(
      {static_cast<std::size_t>(max_players), std::chrono::milliseconds(heartbeat_timeout_ms)});
  devy::server::AuthoritativeLoop authoritative_loop(
      {static_cast<uint32_t>(tick_rate_hz), static_cast<std::size_t>(input_queue_capacity),
       static_cast<uint32_t>(snapshot_interval_ticks)},
      std::chrono::steady_clock::now());
  devy::server::MovementSimulation movement_simulation(
      {static_cast<float>(movement_speed_units_per_second)});
  devy::server::WorldReplication world_replication(
      {map_chunks_x, map_chunks_y, map_chunks_z, map_draw_distance_chunks});
  devy::server::BlockInteraction block_interaction({std::move(valid_block_ids)});
  devy::server::CombatSimulation combat_simulation(
      std::move(weapon_definitions),
      {combat_starting_health, static_cast<float>(combat_hitscan_range_units),
       static_cast<float>(combat_projectile_range_units),
       static_cast<float>(combat_projectile_speed_units_per_second),
       static_cast<float>(combat_hit_radius_units)});
  devy::server::InventoryLootSimulation inventory_loot_simulation(
      std::move(treasure_definitions),
      {static_cast<uint32_t>(inventory_spawn_interval_ticks),
       static_cast<std::size_t>(inventory_max_active_spawns),
       static_cast<std::size_t>(inventory_pickup_queue_capacity),
       static_cast<float>(inventory_pickup_radius_units),
       static_cast<std::size_t>(inventory_max_items_per_player), inventory_max_weight_per_player,
       loot_drop_mode, map_chunks_x * devy::voxel::kChunkSize,
       map_chunks_z * devy::voxel::kChunkSize,
       static_cast<float>(inventory_death_drop_spread_units)});
  devy::server::MatchLifecycleSimulation match_lifecycle_simulation(
      {match_pre_match_ticks, static_cast<uint32_t>(match_duration_seconds),
       static_cast<uint32_t>(match_respawns_per_player), match_respawn_delay_ticks,
       static_cast<uint32_t>(match_min_players_to_start)});
  devy::server::RuntimeTelemetry runtime_telemetry({runtime_profiling_enabled,
                                                    static_cast<uint32_t>(
                                                        runtime_profiling_report_interval_ticks),
                                                    static_cast<std::size_t>(
                                                        runtime_profiling_history_size_ticks),
                                                    runtime_profiling_tick_lag_tolerance_ms,
                                                    {runtime_profiling_alert_max_tick_lag_rate,
                                                     runtime_profiling_alert_max_packet_drop_rate,
                                                     runtime_profiling_alert_max_parse_error_rate,
                                                     static_cast<std::size_t>(
                                                         runtime_profiling_alert_min_active_players)}});
  runtime_telemetry.set_tick_budget(authoritative_loop.tick_interval());
  auto* telemetry = runtime_profiling_enabled ? &runtime_telemetry : nullptr;
  std::unordered_map<std::uintptr_t, ENetPeer*> peers_by_token{};
  nlohmann::json pending_snapshot_events = nlohmann::json::array();
  const auto emit_runtime_report = [&](const devy::server::RuntimeTelemetryReport& report) {
    devy::log::write(devy::log::Level::Info, format_runtime_telemetry_report(report));
    const std::string diagnostics_line = format_runtime_diagnostics_json(report);
    devy::log::write(devy::log::Level::Info, diagnostics_line);

    if (health_file_path.empty()) {
      return;
    }

    const auto diagnostics_payload = diagnostics_json_payload(diagnostics_line);
    if (!diagnostics_payload.has_value()) {
      devy::log::write(devy::log::Level::Warn,
                       "Health diagnostics publish skipped: malformed diagnostics line.");
      return;
    }

    std::string write_error{};
    if (!write_atomic_text_file(health_file_path, diagnostics_payload.value(), &write_error)) {
      devy::log::write(devy::log::Level::Warn,
                       "Health diagnostics publish failed: " + write_error + ".");
    }
  };

  const auto smoke_deadline =
      std::chrono::steady_clock::now() + std::chrono::seconds(smoke_seconds);

  while (running) {
    ENetEvent event;
    while (enet_host_service(server, &event, 16) > 0) {
      switch (event.type) {
      case ENET_EVENT_TYPE_CONNECT: {
        const std::uintptr_t token = peer_token(event.peer);
        peers_by_token[token] = event.peer;
        devy::log::write(devy::log::Level::Info,
                         "Client connected (awaiting join request). peer=" + std::to_string(token));
        break;
      }
      case ENET_EVENT_TYPE_RECEIVE: {
        const std::uintptr_t token = peer_token(event.peer);
        const auto now = std::chrono::steady_clock::now();
        std::string data(reinterpret_cast<char*>(event.packet->data), event.packet->dataLength);
        if (telemetry != nullptr) {
          telemetry->note_inbound_packet_received();
        }
        bool command_rejected = false;
        const auto mark_command_rejected = [&command_rejected]() { command_rejected = true; };
        const devy::net::ParseResult parsed = devy::net::try_deserialize(data);
        if (!parsed.ok()) {
          if (telemetry != nullptr) {
            telemetry->note_inbound_packet_drop(true);
          }
          devy::log::write(devy::log::Level::Warn, std::string("Dropped invalid packet: ") +
                                                       devy::net::to_string(parsed.error) + " (" +
                                                       parsed.detail + ")");
          enet_packet_destroy(event.packet);
          break;
        }

        if (parsed.packet.type == devy::net::MessageType::JoinRequest) {
          const std::string player_name = parsed.packet.payload["player_name"].get<std::string>();
          const devy::server::JoinResult join_result =
              session_manager.handle_join_request(token, player_name, now);

          if (join_result.accepted()) {
            devy::net::Packet response{
                devy::net::MessageType::JoinAccept,
                {{"message", "Welcome to Devy FPS"},
                 {"max_players", max_players},
                 {"protocol_version", devy::net::kProtocolVersion},
                 {"feature_flags",
                  nlohmann::json::array({"handshake_v1", "heartbeat_v1", "movement_snapshot_v1",
                                         "input_ack_v1", "world_replication_v1",
                                         "block_interaction_v1", "weapon_fire_v1",
                                         "damage_pipeline_v1", "death_event_v1",
                                         "inventory_loot_v1", "match_lifecycle_v1"})},
                 {"accepted", true},
                 {"player_id", join_result.session.player_id},
                 {"player_name", join_result.session.player_name},
                 {"join_status", devy::server::to_string(join_result.status)}}};
            send_packet(event.peer, response, telemetry);
            movement_simulation.ensure_player(join_result.session.player_id);
            combat_simulation.ensure_player(join_result.session.player_id);
            inventory_loot_simulation.ensure_player(join_result.session.player_id);
            match_lifecycle_simulation.ensure_player(join_result.session.player_id);
            const auto joined_match_state =
                match_lifecycle_simulation.state(authoritative_loop.tick_interval());
            send_packet(event.peer,
                        devy::server::build_match_state_packet(
                            joined_match_state, match_lifecycle_simulation.scoreboard_snapshot()),
                        telemetry);
            devy::log::write(devy::log::Level::Info,
                             "Join accepted: name=" + join_result.session.player_name +
                                 " player_id=" + std::to_string(join_result.session.player_id) +
                                 " status=" + devy::server::to_string(join_result.status) + ".");
          } else {
            const std::string reason = join_error_message(join_result.error);
            devy::net::Packet response{
                devy::net::MessageType::JoinAccept,
                {{"message", reason},
                 {"max_players", max_players},
                 {"protocol_version", devy::net::kProtocolVersion},
                 {"feature_flags",
                  nlohmann::json::array({"handshake_v1", "heartbeat_v1", "movement_snapshot_v1",
                                         "input_ack_v1", "world_replication_v1",
                                         "block_interaction_v1", "weapon_fire_v1",
                                         "damage_pipeline_v1", "death_event_v1",
                                         "inventory_loot_v1", "match_lifecycle_v1"})},
                 {"accepted", false},
                 {"reason", devy::server::to_string(join_result.error)}}};
            send_packet(event.peer, response, telemetry);
            devy::log::write(devy::log::Level::Warn,
                             "Join rejected for peer=" + std::to_string(token) + ": " +
                                 devy::server::to_string(join_result.error) + ".");
            mark_command_rejected();
            enet_peer_disconnect(event.peer, 0);
          }
        } else if (parsed.packet.type == devy::net::MessageType::Heartbeat) {
          const auto session = session_manager.session_for_peer(token);
          if (!session.has_value()) {
            devy::log::write(devy::log::Level::Warn, "Heartbeat received before join from peer=" +
                                                         std::to_string(token) + ".");
            mark_command_rejected();
          } else {
            const auto heartbeat_player_id = json_to_u32(parsed.packet.payload["player_id"]);
            if (!heartbeat_player_id.has_value()) {
              devy::log::write(devy::log::Level::Warn,
                               "Heartbeat payload had invalid player_id range for peer=" +
                                   std::to_string(token) + ".");
              mark_command_rejected();
            } else if (heartbeat_player_id.value() != session->player_id) {
              devy::log::write(devy::log::Level::Warn, "Heartbeat player_id mismatch for peer=" +
                                                           std::to_string(token) + ".");
              mark_command_rejected();
            } else {
              session_manager.handle_heartbeat(token, now);
            }
          }
        } else if (parsed.packet.type == devy::net::MessageType::PlayerInput) {
          const auto session = session_manager.session_for_peer(token);
          if (!session.has_value()) {
            devy::log::write(
                devy::log::Level::Warn,
                "Player input received before join from peer=" + std::to_string(token) + ".");
            mark_command_rejected();
          } else {
            const auto input_player_id = json_to_u32(parsed.packet.payload["player_id"]);
            const auto input_seq = json_to_u32(parsed.packet.payload["input_seq"]);
            const auto move_x = json_to_float(parsed.packet.payload["move_x"]);
            const auto move_y = json_to_float(parsed.packet.payload["move_y"]);
            if (!input_player_id.has_value() || !input_seq.has_value() || !move_x.has_value() ||
                !move_y.has_value()) {
              devy::log::write(devy::log::Level::Warn,
                               "Player input payload range validation failed for peer=" +
                                   std::to_string(token) + ".");
              mark_command_rejected();
            } else if (input_player_id.value() != session->player_id) {
              devy::log::write(devy::log::Level::Warn, "Player input player_id mismatch for peer=" +
                                                           std::to_string(token) + ".");
              mark_command_rejected();
            } else {
              devy::server::PlayerInputCommand input{};
              input.player_id = input_player_id.value();
              input.input_seq = input_seq.value();
              input.move_x = move_x.value();
              input.move_y = move_y.value();
              input.jump = parsed.packet.payload["jump"].get<bool>();
              input.fire = parsed.packet.payload["fire"].get<bool>();
              input.received_at = now;
              const auto enqueue_status = authoritative_loop.enqueue_input(input);
              if (enqueue_status != devy::server::InputEnqueueStatus::Accepted) {
                devy::log::write(
                    devy::log::Level::Warn,
                    "Dropped player input for player_id=" + std::to_string(input.player_id) +
                        " status=" + devy::server::to_string(enqueue_status) + ".");
                mark_command_rejected();
              }
              session_manager.handle_heartbeat(token, now);
            }
          }
        } else if (parsed.packet.type == devy::net::MessageType::BlockUpdate) {
          const auto session = session_manager.session_for_peer(token);
          if (!session.has_value()) {
            devy::log::write(
                devy::log::Level::Warn,
                "Block update received before join from peer=" + std::to_string(token) + ".");
            mark_command_rejected();
          } else {
            uint32_t command_player_id = session->player_id;
            bool player_id_valid = true;
            if (parsed.packet.payload.contains("player_id")) {
              const auto payload_player_id = json_to_u32(parsed.packet.payload["player_id"]);
              if (!payload_player_id.has_value()) {
                devy::log::write(devy::log::Level::Warn,
                                 "Block update payload had invalid player_id for peer=" +
                                     std::to_string(token) + ".");
                player_id_valid = false;
                mark_command_rejected();
              } else {
                command_player_id = payload_player_id.value();
              }
            }

            const auto world_x = json_to_i32(parsed.packet.payload["x"]);
            const auto world_y = json_to_i32(parsed.packet.payload["y"]);
            const auto world_z = json_to_i32(parsed.packet.payload["z"]);
            const auto block_id = json_to_u8(parsed.packet.payload["block_id"]);
            if (!player_id_valid) {
              // Invalid optional player_id already logged above.
            } else if (!world_x.has_value() || !world_y.has_value() || !world_z.has_value() ||
                       !block_id.has_value()) {
              devy::log::write(devy::log::Level::Warn,
                               "Block update payload range validation failed for peer=" +
                                   std::to_string(token) + ".");
              mark_command_rejected();
            } else if (command_player_id != session->player_id) {
              devy::log::write(devy::log::Level::Warn, "Block update player_id mismatch for peer=" +
                                                           std::to_string(token) + ".");
              mark_command_rejected();
            } else {
              const devy::server::BlockUpdateOutcome outcome =
                  block_interaction.apply({command_player_id, world_x.value(), world_y.value(),
                                           world_z.value(), block_id.value()},
                                          world);
              if (outcome.applied()) {
                world_replication.mark_chunk_dirty(
                    {outcome.chunk_x, outcome.chunk_y, outcome.chunk_z});
              } else {
                devy::log::write(
                    devy::log::Level::Warn,
                    "Rejected block update for player_id=" + std::to_string(command_player_id) +
                        " status=" + devy::server::to_string(outcome.status) + ".");
                mark_command_rejected();
              }
              session_manager.handle_heartbeat(token, now);
            }
          }
        } else if (parsed.packet.type == devy::net::MessageType::WeaponFire) {
          const auto session = session_manager.session_for_peer(token);
          if (!session.has_value()) {
            devy::log::write(devy::log::Level::Warn, "Weapon fire received before join from peer=" +
                                                         std::to_string(token) + ".");
            mark_command_rejected();
          } else {
            const auto current_match_state =
                match_lifecycle_simulation.state(authoritative_loop.tick_interval());
            if (current_match_state.phase != devy::server::MatchPhase::InMatch) {
              devy::log::write(devy::log::Level::Warn,
                               "Rejected weapon fire while match not active for peer=" +
                                   std::to_string(token) + ".");
              mark_command_rejected();
              session_manager.handle_heartbeat(token, now);
              if (telemetry != nullptr) {
                telemetry->note_command_rejected();
                telemetry->note_inbound_packet_drop(false);
              }
              enet_packet_destroy(event.packet);
              break;
            }

            const auto fire_player_id = json_to_u32(parsed.packet.payload["player_id"]);
            const auto shot_seq = json_to_u32(parsed.packet.payload["shot_seq"]);
            const auto& weapon_id = parsed.packet.payload["weapon_id"];
            const auto& origin = parsed.packet.payload["origin"];
            const auto& direction = parsed.packet.payload["direction"];

            std::optional<float> origin_x{};
            std::optional<float> origin_y{};
            std::optional<float> direction_x{};
            std::optional<float> direction_y{};
            if (origin.is_object()) {
              origin_x = json_to_float(origin.value("x", nlohmann::json{}));
              origin_y = json_to_float(origin.value("y", nlohmann::json{}));
            }
            if (direction.is_object()) {
              direction_x = json_to_float(direction.value("x", nlohmann::json{}));
              direction_y = json_to_float(direction.value("y", nlohmann::json{}));
            }

            if (!fire_player_id.has_value() || !shot_seq.has_value() || !weapon_id.is_string() ||
                !origin_x.has_value() || !origin_y.has_value() || !direction_x.has_value() ||
                !direction_y.has_value()) {
              devy::log::write(
                  devy::log::Level::Warn,
                  "Weapon fire payload validation failed for peer=" + std::to_string(token) + ".");
              mark_command_rejected();
            } else if (fire_player_id.value() != session->player_id) {
              devy::log::write(devy::log::Level::Warn, "Weapon fire player_id mismatch for peer=" +
                                                           std::to_string(token) + ".");
              mark_command_rejected();
            } else {
              const devy::server::FireEnqueueStatus enqueue_status = combat_simulation.enqueue_fire(
                  {fire_player_id.value(), shot_seq.value(), weapon_id.get<std::string>(),
                   origin_x.value(), origin_y.value(), direction_x.value(), direction_y.value(),
                   now});
              if (enqueue_status != devy::server::FireEnqueueStatus::Accepted) {
                devy::log::write(devy::log::Level::Warn,
                                 "Rejected weapon fire command for player_id=" +
                                     std::to_string(fire_player_id.value()) +
                                     " status=" + devy::server::to_string(enqueue_status) + ".");
                mark_command_rejected();
              }
              session_manager.handle_heartbeat(token, now);
            }
          }
        } else if (parsed.packet.type == devy::net::MessageType::TreasurePickup) {
          const auto session = session_manager.session_for_peer(token);
          if (!session.has_value()) {
            devy::log::write(
                devy::log::Level::Warn,
                "Treasure pickup received before join from peer=" + std::to_string(token) + ".");
            mark_command_rejected();
          } else {
            const auto current_match_state =
                match_lifecycle_simulation.state(authoritative_loop.tick_interval());
            if (current_match_state.phase != devy::server::MatchPhase::InMatch) {
              devy::log::write(devy::log::Level::Warn,
                               "Rejected treasure pickup while match not active for peer=" +
                                   std::to_string(token) + ".");
              mark_command_rejected();
              session_manager.handle_heartbeat(token, now);
            } else {
              const auto pickup_player_id = json_to_u32(parsed.packet.payload["player_id"]);
              const auto pickup_seq = json_to_u32(parsed.packet.payload["pickup_seq"]);
              const auto spawn_id = json_to_u64(parsed.packet.payload["spawn_id"]);
              if (!pickup_player_id.has_value() || !pickup_seq.has_value() ||
                  !spawn_id.has_value()) {
                devy::log::write(devy::log::Level::Warn,
                                 "Treasure pickup payload validation failed for peer=" +
                                     std::to_string(token) + ".");
                mark_command_rejected();
              } else if (pickup_player_id.value() != session->player_id) {
                devy::log::write(
                    devy::log::Level::Warn,
                    "Treasure pickup player_id mismatch for peer=" + std::to_string(token) + ".");
                mark_command_rejected();
              } else {
                const auto enqueue_status = inventory_loot_simulation.enqueue_pickup(
                    {pickup_player_id.value(), pickup_seq.value(), spawn_id.value(), now});
                if (enqueue_status != devy::server::TreasurePickupEnqueueStatus::Accepted) {
                  devy::log::write(devy::log::Level::Warn,
                                   "Rejected treasure pickup command for player_id=" +
                                       std::to_string(pickup_player_id.value()) +
                                       " status=" + devy::server::to_string(enqueue_status) + ".");
                  mark_command_rejected();
                }
                session_manager.handle_heartbeat(token, now);
              }
            }
          }
        } else {
          if (!session_manager.handle_heartbeat(token, now)) {
            devy::log::write(devy::log::Level::Warn,
                             "Dropping pre-join message type `" +
                                 std::string(devy::net::to_string(parsed.packet.type)) +
                                 "` from peer=" + std::to_string(token) + ".");
            mark_command_rejected();
          }
        }
        if (command_rejected && telemetry != nullptr) {
          telemetry->note_command_rejected();
          telemetry->note_inbound_packet_drop(false);
        }
        enet_packet_destroy(event.packet);
        break;
      }
      case ENET_EVENT_TYPE_DISCONNECT: {
        const std::uintptr_t token = peer_token(event.peer);
        peers_by_token.erase(token);
        const auto session = session_manager.handle_disconnect(token);
        if (session.has_value()) {
          authoritative_loop.clear_player(session->player_id);
          movement_simulation.remove_player(session->player_id);
          world_replication.remove_player(session->player_id);
          combat_simulation.remove_player(session->player_id);
          inventory_loot_simulation.remove_player(session->player_id);
          match_lifecycle_simulation.remove_player(session->player_id);
          devy::log::write(devy::log::Level::Info,
                           "Client disconnected: name=" + session->player_name +
                               " player_id=" + std::to_string(session->player_id) + ".");
        } else {
          devy::log::write(devy::log::Level::Info, "Client disconnected.");
        }
        break;
      }
      default:
        break;
      }
    }

    const auto timed_out = session_manager.collect_timed_out(std::chrono::steady_clock::now());
    for (const auto& session : timed_out) {
      authoritative_loop.clear_player(session.player_id);
      movement_simulation.remove_player(session.player_id);
      world_replication.remove_player(session.player_id);
      combat_simulation.remove_player(session.player_id);
      inventory_loot_simulation.remove_player(session.player_id);
      match_lifecycle_simulation.remove_player(session.player_id);
      auto peer_it = peers_by_token.find(session.peer_token);
      if (peer_it != peers_by_token.end()) {
        enet_peer_disconnect(peer_it->second, 0);
        peers_by_token.erase(peer_it);
      }
      devy::log::write(devy::log::Level::Warn,
                       "Session timed out: name=" + session.player_name +
                           " player_id=" + std::to_string(session.player_id) + ".");
    }

    const auto tick_frames = authoritative_loop.advance(std::chrono::steady_clock::now());
    for (const auto& frame : tick_frames) {
      const auto tick_started_at = std::chrono::steady_clock::now();
      if (telemetry != nullptr) {
        telemetry->begin_tick(frame.tick, tick_started_at);
      }

      const auto movement_phase_started = std::chrono::steady_clock::now();
      movement_simulation.apply_inputs(authoritative_loop.tick_interval(), frame.inputs);
      const auto movement_states = movement_simulation.snapshot();
      if (telemetry != nullptr) {
        telemetry->note_phase(devy::server::RuntimeTickPhase::Movement,
                              std::chrono::steady_clock::now() - movement_phase_started);
      }

      const auto combat_phase_started = std::chrono::steady_clock::now();
      combat_simulation.set_player_positions(movement_states);
      const auto combat_result =
          combat_simulation.resolve_tick(frame.tick, authoritative_loop.tick_interval());
      const auto combat_states_before_respawn = combat_simulation.snapshot();
      if (telemetry != nullptr) {
        telemetry->note_phase(devy::server::RuntimeTickPhase::Combat,
                              std::chrono::steady_clock::now() - combat_phase_started);
      }

      const auto inventory_phase_started = std::chrono::steady_clock::now();
      const auto inventory_result = inventory_loot_simulation.resolve_tick(
          frame.tick, movement_states, combat_states_before_respawn, combat_result.death_events);
      const auto active_sessions = session_manager.active_sessions();
      const auto inventory_states = inventory_loot_simulation.inventory_snapshot();
      if (telemetry != nullptr) {
        telemetry->note_phase(devy::server::RuntimeTickPhase::Inventory,
                              std::chrono::steady_clock::now() - inventory_phase_started);
      }

      std::vector<uint32_t> active_player_ids{};
      active_player_ids.reserve(active_sessions.size());
      for (const auto& session : active_sessions) {
        active_player_ids.push_back(session.player_id);
      }

      const auto match_phase_started = std::chrono::steady_clock::now();
      auto match_result = match_lifecycle_simulation.resolve_tick(
          frame.tick, authoritative_loop.tick_interval(), active_player_ids,
          combat_states_before_respawn, combat_result.death_events, inventory_states);
      if (telemetry != nullptr) {
        telemetry->note_phase(devy::server::RuntimeTickPhase::MatchLifecycle,
                              std::chrono::steady_clock::now() - match_phase_started);
      }

      for (uint32_t player_id : match_result.respawned_players) {
        combat_simulation.respawn_player(player_id);
        pending_snapshot_events.push_back(
            {{"type", "respawn_event"}, {"tick", frame.tick}, {"player_id", player_id}});
      }
      if (match_result.state_changed) {
        nlohmann::json state_event = {{"type", "match_state_changed"},
                                      {"tick", frame.tick},
                                      {"state", devy::server::to_string(match_result.state.phase)},
                                      {"remaining_seconds", match_result.state.remaining_seconds}};
        if (match_result.state.winner_player_id.has_value()) {
          state_event["winner_player_id"] = match_result.state.winner_player_id.value();
        }
        pending_snapshot_events.push_back(std::move(state_event));
      }

      const auto combat_states = combat_simulation.snapshot();
      const auto broadcast_phase_started = std::chrono::steady_clock::now();
      std::vector<devy::net::Packet> reliable_broadcasts{};
      append_combat_outputs(combat_result, pending_snapshot_events, &reliable_broadcasts);
      for (const auto& spawn : inventory_result.spawned) {
        pending_snapshot_events.push_back(to_snapshot_event(frame.tick, spawn));
      }
      for (const auto& pickup : inventory_result.pickup_results) {
        pending_snapshot_events.push_back(to_snapshot_event(frame.tick, pickup));
      }
      for (const auto& broadcast : reliable_broadcasts) {
        for (const auto& session : active_sessions) {
          auto peer_it = peers_by_token.find(session.peer_token);
          if (peer_it != peers_by_token.end()) {
            send_packet(peer_it->second, broadcast, telemetry);
          }
        }
      }
      if (!inventory_result.inventory_deltas.empty()) {
        std::unordered_map<uint32_t, ENetPeer*> peers_by_player_id{};
        peers_by_player_id.reserve(active_sessions.size());
        for (const auto& session : active_sessions) {
          auto peer_it = peers_by_token.find(session.peer_token);
          if (peer_it != peers_by_token.end()) {
            peers_by_player_id[session.player_id] = peer_it->second;
          }
        }
        for (const auto& summary : inventory_result.inventory_deltas) {
          auto peer_it = peers_by_player_id.find(summary.player_id);
          if (peer_it == peers_by_player_id.end()) {
            continue;
          }
          send_packet(peer_it->second, inventory_update_packet(summary), telemetry);
        }
      }
      if (match_result.state_changed ||
          (frame.snapshot_due && runtime_match_state_broadcast_on_snapshot)) {
        const auto match_packet =
            devy::server::build_match_state_packet(match_result.state, match_result.scoreboard);
        for (const auto& session : active_sessions) {
          auto peer_it = peers_by_token.find(session.peer_token);
          if (peer_it != peers_by_token.end()) {
            send_packet(peer_it->second, match_packet, telemetry);
          }
        }
      }
      if (telemetry != nullptr) {
        telemetry->note_phase(devy::server::RuntimeTickPhase::Broadcast,
                              std::chrono::steady_clock::now() - broadcast_phase_started);
      }

      const auto active_treasure_spawns = inventory_loot_simulation.active_spawns();
      if (telemetry != nullptr) {
        telemetry->note_resources({active_sessions.size(), movement_states.size(),
                                   authoritative_loop.pending_input_count(),
                                   pending_snapshot_events.size(), active_treasure_spawns.size()});
      }

      if (!frame.snapshot_due) {
        if (telemetry != nullptr) {
          const auto report = telemetry->end_tick(std::chrono::steady_clock::now());
          if (report.has_value()) {
            emit_runtime_report(report.value());
          }
        }
        continue;
      }

      const auto snapshot_build_phase_started = std::chrono::steady_clock::now();
      struct SnapshotPlayerView {
        uint32_t player_id{0U};
        float position_x{0.0F};
        float position_y{0.0F};
        float velocity_x{0.0F};
        float velocity_y{0.0F};
        uint32_t last_processed_input_seq{0U};
        int health{0};
        bool alive{true};
        uint32_t last_shot_seq{0U};
        int coins{0};
        int inventory_weight{0};
        uint32_t inventory_items{0U};
        uint32_t last_pickup_seq{0U};
        uint32_t respawns_remaining{0U};
        bool eliminated{false};
      };

      std::vector<SnapshotPlayerView> snapshot_players{};
      snapshot_players.reserve(active_sessions.size());
      std::vector<int32_t> session_index_by_player_id(
          static_cast<std::size_t>(max_players) + 1U, -1);
      for (std::size_t session_index = 0; session_index < active_sessions.size(); ++session_index) {
        const auto& session = active_sessions[session_index];
        snapshot_players.push_back({session.player_id, 0.0F,
                                    0.0F,
                                    0.0F,
                                    0.0F,
                                    0U,
                                    combat_starting_health,
                                    true,
                                    0U,
                                    0,
                                    0,
                                    0U,
                                    0U,
                                    static_cast<uint32_t>(match_respawns_per_player),
                                    false});
        if (session.player_id < session_index_by_player_id.size()) {
          session_index_by_player_id[session.player_id] = static_cast<int32_t>(session_index);
        }
      }

      for (const auto& state : movement_states) {
        if (state.player_id >= session_index_by_player_id.size()) {
          continue;
        }
        const int32_t session_index = session_index_by_player_id[state.player_id];
        if (session_index < 0) {
          continue;
        }
        auto& player = snapshot_players[static_cast<std::size_t>(session_index)];
        player.position_x = state.position_x;
        player.position_y = state.position_y;
        player.velocity_x = state.velocity_x;
        player.velocity_y = state.velocity_y;
        player.last_processed_input_seq = state.last_processed_input_seq;
      }
      for (const auto& state : combat_states) {
        if (state.player_id >= session_index_by_player_id.size()) {
          continue;
        }
        const int32_t session_index = session_index_by_player_id[state.player_id];
        if (session_index < 0) {
          continue;
        }
        auto& player = snapshot_players[static_cast<std::size_t>(session_index)];
        player.health = state.health;
        player.alive = state.alive;
        player.last_shot_seq = state.last_shot_seq;
      }
      for (const auto& state : inventory_states) {
        if (state.player_id >= session_index_by_player_id.size()) {
          continue;
        }
        const int32_t session_index = session_index_by_player_id[state.player_id];
        if (session_index < 0) {
          continue;
        }
        auto& player = snapshot_players[static_cast<std::size_t>(session_index)];
        player.coins = state.total_value;
        player.inventory_weight = state.total_weight;
        player.inventory_items = state.item_count;
        player.last_pickup_seq = state.last_pickup_seq;
      }
      for (const auto& score : match_result.scoreboard) {
        if (score.player_id >= session_index_by_player_id.size()) {
          continue;
        }
        const int32_t session_index = session_index_by_player_id[score.player_id];
        if (session_index < 0) {
          continue;
        }
        auto& player = snapshot_players[static_cast<std::size_t>(session_index)];
        player.respawns_remaining = score.respawns_remaining;
        player.eliminated = score.eliminated;
      }

      nlohmann::json players = nlohmann::json::array();
      players.get_ref<nlohmann::json::array_t&>().reserve(snapshot_players.size());
      std::vector<std::pair<float, float>> player_positions_by_session_index{};
      player_positions_by_session_index.reserve(snapshot_players.size());
      for (std::size_t session_index = 0; session_index < snapshot_players.size(); ++session_index) {
        const auto& player = snapshot_players[session_index];
        const auto& session = active_sessions[session_index];
        players.push_back({{"player_id", player.player_id},
                           {"player_name", session.player_name},
                           {"position", {{"x", player.position_x}, {"y", player.position_y}}},
                           {"velocity", {{"x", player.velocity_x}, {"y", player.velocity_y}}},
                           {"last_processed_input_seq", player.last_processed_input_seq},
                           {"health", player.health},
                           {"alive", player.alive},
                           {"last_shot_seq", player.last_shot_seq},
                           {"coins", player.coins},
                           {"inventory_weight", player.inventory_weight},
                           {"inventory_items", player.inventory_items},
                           {"last_pickup_seq", player.last_pickup_seq},
                           {"respawns_remaining", player.respawns_remaining},
                           {"eliminated", player.eliminated}});
        player_positions_by_session_index.emplace_back(player.position_x, player.position_y);
      }
      const nlohmann::json treasure_spawns = spawns_to_json(active_treasure_spawns);
      const nlohmann::json match_state = devy::server::build_match_state_payload(
          match_result.state, match_result.scoreboard, runtime_snapshot_include_match_scoreboard);

      if (telemetry != nullptr) {
        telemetry->note_phase(devy::server::RuntimeTickPhase::SnapshotBuild,
                              std::chrono::steady_clock::now() - snapshot_build_phase_started);
      }

      const auto snapshot_send_phase_started = std::chrono::steady_clock::now();
      for (std::size_t session_index = 0; session_index < active_sessions.size(); ++session_index) {
        const auto& session = active_sessions[session_index];
        auto peer_it = peers_by_token.find(session.peer_token);
        if (peer_it == peers_by_token.end()) {
          continue;
        }

        const auto& position = player_positions_by_session_index[session_index];
        const float session_position_x = position.first;
        // Movement simulation is currently planar (x/y); replication maps y onto world-z.
        const float session_position_z = position.second;

        const auto world_update = world_replication.build_player_update(
            session.player_id, session_position_x, session_position_z, world);
        nlohmann::json chunk_sync = {{"added", chunk_revisions_to_json(world_update.added)},
                                     {"removed", chunk_revisions_to_json(world_update.removed)},
                                     {"deltas", chunk_revisions_to_json(world_update.deltas)}};
        devy::net::Packet snapshot{devy::net::MessageType::StateSnapshot,
                                   {{"tick", frame.tick},
                                    {"players", players},
                                    {"events", pending_snapshot_events},
                                    {"chunk_sync", std::move(chunk_sync)},
                                    {"treasure_spawns", treasure_spawns},
                                    {"match_state", match_state}},
                                   devy::net::kProtocolVersion};
        send_packet(peer_it->second, snapshot, telemetry);
      }
      if (telemetry != nullptr) {
        telemetry->note_phase(devy::server::RuntimeTickPhase::SnapshotSend,
                              std::chrono::steady_clock::now() - snapshot_send_phase_started);
      }
      pending_snapshot_events = nlohmann::json::array();

      if (telemetry != nullptr) {
        const auto report = telemetry->end_tick(std::chrono::steady_clock::now());
        if (report.has_value()) {
          emit_runtime_report(report.value());
        }
      }
    }

    if (smoke_seconds > 0 && std::chrono::steady_clock::now() >= smoke_deadline) {
      running = false;
    }

    std::this_thread::sleep_for(std::chrono::milliseconds(5));
  }

  enet_host_destroy(server);
  enet_deinitialize();
  devy::log::write(devy::log::Level::Info, "Server stopped.");
  return 0;
}
