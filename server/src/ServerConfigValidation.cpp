#include "server/ServerConfigValidation.h"
#include "shared/voxel/WorldGenerationProfile.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdint>
#include <limits>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace devy::server {
namespace {

struct IntReadResult {
  bool present{false};
  bool valid{true};
  int64_t value{0};
};

struct NumberReadResult {
  bool present{false};
  bool valid{true};
  double value{0.0};
};

struct BoolReadResult {
  bool present{false};
  bool valid{true};
  bool value{false};
};

struct StringReadResult {
  bool present{false};
  bool valid{true};
  std::string value{};
};

void add_error(std::vector<std::string>& errors, const std::string& path, const std::string& detail) {
  errors.push_back(path + ": " + detail);
}

std::string lowercase_string(const std::string& value) {
  std::string out = value;
  std::transform(out.begin(), out.end(), out.begin(), [](unsigned char c) {
    return static_cast<char>(std::tolower(c));
  });
  return out;
}

std::optional<int64_t> to_i64(const nlohmann::json& value) {
  if (value.is_number_integer()) {
    return value.get<int64_t>();
  }
  if (value.is_number_unsigned()) {
    const auto raw = value.get<uint64_t>();
    if (raw > static_cast<uint64_t>(std::numeric_limits<int64_t>::max())) {
      return std::nullopt;
    }
    return static_cast<int64_t>(raw);
  }
  return std::nullopt;
}

IntReadResult read_integer_field(const nlohmann::json& parent, std::string_view key,
                                 std::string_view path, std::vector<std::string>& errors) {
  IntReadResult out{};
  if (!parent.contains(std::string(key))) {
    return out;
  }
  out.present = true;
  const auto& value = parent[std::string(key)];
  const auto raw = to_i64(value);
  if (!raw.has_value()) {
    out.valid = false;
    add_error(errors, std::string(path), "expected integer value.");
    return out;
  }
  out.value = raw.value();
  return out;
}

NumberReadResult read_number_field(const nlohmann::json& parent, std::string_view key,
                                   std::string_view path, std::vector<std::string>& errors) {
  NumberReadResult out{};
  if (!parent.contains(std::string(key))) {
    return out;
  }
  out.present = true;
  const auto& value = parent[std::string(key)];
  if (!value.is_number()) {
    out.valid = false;
    add_error(errors, std::string(path), "expected numeric value.");
    return out;
  }
  out.value = value.get<double>();
  if (!std::isfinite(out.value)) {
    out.valid = false;
    add_error(errors, std::string(path), "expected finite numeric value.");
  }
  return out;
}

BoolReadResult read_bool_field(const nlohmann::json& parent, std::string_view key,
                               std::string_view path, std::vector<std::string>& errors) {
  BoolReadResult out{};
  if (!parent.contains(std::string(key))) {
    return out;
  }
  out.present = true;
  const auto& value = parent[std::string(key)];
  if (!value.is_boolean()) {
    out.valid = false;
    add_error(errors, std::string(path), "expected boolean value.");
    return out;
  }
  out.value = value.get<bool>();
  return out;
}

StringReadResult read_string_field(const nlohmann::json& parent, std::string_view key,
                                   std::string_view path, std::vector<std::string>& errors) {
  StringReadResult out{};
  if (!parent.contains(std::string(key))) {
    return out;
  }
  out.present = true;
  const auto& value = parent[std::string(key)];
  if (!value.is_string()) {
    out.valid = false;
    add_error(errors, std::string(path), "expected string value.");
    return out;
  }
  out.value = value.get<std::string>();
  return out;
}

const nlohmann::json* read_object_field(const nlohmann::json& parent, std::string_view key,
                                        std::string_view path, std::vector<std::string>& errors) {
  if (!parent.contains(std::string(key))) {
    return nullptr;
  }
  const auto& value = parent[std::string(key)];
  if (!value.is_object()) {
    add_error(errors, std::string(path), "expected object.");
    return nullptr;
  }
  return &value;
}

void validate_positive_integer(const nlohmann::json& parent, std::string_view key,
                               std::string_view path, std::vector<std::string>& errors) {
  const auto result = read_integer_field(parent, key, path, errors);
  if (result.present && result.valid && result.value <= 0) {
    add_error(errors, std::string(path), "must be greater than zero.");
  }
}

void validate_non_negative_integer(const nlohmann::json& parent, std::string_view key,
                                   std::string_view path, std::vector<std::string>& errors) {
  const auto result = read_integer_field(parent, key, path, errors);
  if (result.present && result.valid && result.value < 0) {
    add_error(errors, std::string(path), "must be zero or greater.");
  }
}

void validate_positive_number(const nlohmann::json& parent, std::string_view key,
                              std::string_view path, std::vector<std::string>& errors) {
  const auto result = read_number_field(parent, key, path, errors);
  if (result.present && result.valid && result.value <= 0.0) {
    add_error(errors, std::string(path), "must be greater than zero.");
  }
}

void validate_non_negative_number(const nlohmann::json& parent, std::string_view key,
                                  std::string_view path, std::vector<std::string>& errors) {
  const auto result = read_number_field(parent, key, path, errors);
  if (result.present && result.valid && result.value < 0.0) {
    add_error(errors, std::string(path), "must be zero or greater.");
  }
}

void validate_ratio_number(const nlohmann::json& parent, std::string_view key,
                           std::string_view path, std::vector<std::string>& errors) {
  const auto result = read_number_field(parent, key, path, errors);
  if (result.present && result.valid && (result.value < 0.0 || result.value > 1.0)) {
    add_error(errors, std::string(path), "must be between 0.0 and 1.0.");
  }
}

} // namespace

std::vector<std::string> validate_server_config(const nlohmann::json& config) {
  std::vector<std::string> errors{};
  if (!config.is_object()) {
    add_error(errors, "<root>", "expected top-level JSON object.");
    return errors;
  }

  validate_positive_integer(config, "max_players", "max_players", errors);
  const auto port = read_integer_field(config, "port", "port", errors);
  if (port.present && port.valid && (port.value <= 0 || port.value > 65535)) {
    add_error(errors, "port", "must be between 1 and 65535.");
  }
  if (const auto* connection = read_object_field(config, "connection", "connection", errors);
      connection != nullptr) {
    static_cast<void>(read_bool_field(*connection, "port_auto_discovery",
                                      "connection.port_auto_discovery", errors));
    const auto runtime_port_file =
        read_string_field(*connection, "runtime_port_file", "connection.runtime_port_file", errors);
    if (runtime_port_file.present && runtime_port_file.valid && runtime_port_file.value.empty()) {
      add_error(errors, "connection.runtime_port_file", "must not be empty.");
    }
  }

  const auto loot_drop = read_string_field(config, "loot_drop", "loot_drop", errors);
  if (loot_drop.present && loot_drop.valid) {
    const auto lowered = lowercase_string(loot_drop.value);
    if (lowered != "all" && lowered != "none") {
      add_error(errors, "loot_drop", "must be one of: all, none.");
    }
  }

  if (const auto* session = read_object_field(config, "session", "session", errors);
      session != nullptr) {
    validate_positive_integer(*session, "heartbeat_timeout_ms", "session.heartbeat_timeout_ms", errors);
  }

  if (const auto* runtime = read_object_field(config, "runtime", "runtime", errors);
      runtime != nullptr) {
    validate_positive_integer(*runtime, "tick_rate_hz", "runtime.tick_rate_hz", errors);
    validate_positive_integer(*runtime, "snapshot_interval_ticks", "runtime.snapshot_interval_ticks", errors);
    validate_positive_integer(*runtime, "input_queue_capacity", "runtime.input_queue_capacity", errors);
    validate_positive_number(*runtime, "movement_speed_units_per_second",
                             "runtime.movement_speed_units_per_second", errors);
    if (const auto* movement = read_object_field(*runtime, "movement", "runtime.movement", errors);
        movement != nullptr) {
      validate_positive_number(*movement, "accel_ground", "runtime.movement.accel_ground", errors);
      validate_positive_number(*movement, "accel_air", "runtime.movement.accel_air", errors);
      validate_non_negative_number(*movement, "friction_ground", "runtime.movement.friction_ground",
                                   errors);
      validate_positive_number(*movement, "max_speed_walk", "runtime.movement.max_speed_walk",
                               errors);
      validate_positive_number(*movement, "sprint_multiplier",
                               "runtime.movement.sprint_multiplier", errors);
      validate_positive_number(*movement, "crouch_multiplier",
                               "runtime.movement.crouch_multiplier", errors);
      validate_positive_number(*movement, "jump_velocity", "runtime.movement.jump_velocity", errors);
      validate_positive_number(*movement, "gravity", "runtime.movement.gravity", errors);
    }
    if (const auto* camera = read_object_field(*runtime, "camera", "runtime.camera", errors);
        camera != nullptr) {
      validate_positive_number(*camera, "mouse_sensitivity", "runtime.camera.mouse_sensitivity",
                               errors);
      validate_positive_number(*camera, "base_fov_degrees", "runtime.camera.base_fov_degrees",
                               errors);
      validate_non_negative_number(*camera, "sprint_fov_bonus_degrees",
                                   "runtime.camera.sprint_fov_bonus_degrees", errors);
      validate_positive_number(*camera, "height_smoothing", "runtime.camera.height_smoothing",
                               errors);
    }
    static_cast<void>(read_bool_field(*runtime, "match_state_broadcast_on_snapshot",
                                      "runtime.match_state_broadcast_on_snapshot", errors));
    static_cast<void>(read_bool_field(*runtime, "snapshot_include_match_scoreboard",
                                      "runtime.snapshot_include_match_scoreboard", errors));

    int64_t report_interval_ticks = 300;
    int64_t history_size_ticks = 600;
    if (const auto* profiling = read_object_field(*runtime, "profiling", "runtime.profiling", errors);
        profiling != nullptr) {
      static_cast<void>(read_bool_field(*profiling, "enabled", "runtime.profiling.enabled", errors));

      const auto report = read_integer_field(*profiling, "report_interval_ticks",
                                             "runtime.profiling.report_interval_ticks", errors);
      if (report.present && report.valid) {
        report_interval_ticks = report.value;
        if (report_interval_ticks <= 0) {
          add_error(errors, "runtime.profiling.report_interval_ticks", "must be greater than zero.");
        }
      }

      const auto history = read_integer_field(*profiling, "history_size_ticks",
                                              "runtime.profiling.history_size_ticks", errors);
      if (history.present && history.valid) {
        history_size_ticks = history.value;
        if (history_size_ticks <= 0) {
          add_error(errors, "runtime.profiling.history_size_ticks", "must be greater than zero.");
        }
      }

      validate_non_negative_number(*profiling, "tick_lag_tolerance_ms",
                                   "runtime.profiling.tick_lag_tolerance_ms", errors);

      if (const auto* alerts =
              read_object_field(*profiling, "alerts", "runtime.profiling.alerts", errors);
          alerts != nullptr) {
        validate_ratio_number(*alerts, "max_tick_lag_rate",
                              "runtime.profiling.alerts.max_tick_lag_rate", errors);
        validate_ratio_number(*alerts, "max_packet_drop_rate",
                              "runtime.profiling.alerts.max_packet_drop_rate", errors);
        validate_ratio_number(*alerts, "max_parse_error_rate",
                              "runtime.profiling.alerts.max_parse_error_rate", errors);
        validate_non_negative_integer(*alerts, "min_active_players",
                                      "runtime.profiling.alerts.min_active_players", errors);
      }

      if (history_size_ticks > 0 && report_interval_ticks > 0 &&
          history_size_ticks < report_interval_ticks) {
        add_error(errors, "runtime.profiling.history_size_ticks",
                  "must be greater than or equal to report_interval_ticks.");
      }
    }
  }

  if (const auto* combat = read_object_field(config, "combat", "combat", errors);
      combat != nullptr) {
    validate_positive_integer(*combat, "starting_health", "combat.starting_health", errors);
    validate_positive_number(*combat, "hitscan_range_units", "combat.hitscan_range_units", errors);
    validate_positive_number(*combat, "projectile_range_units", "combat.projectile_range_units", errors);
    validate_positive_number(*combat, "projectile_speed_units_per_second",
                             "combat.projectile_speed_units_per_second", errors);
    validate_positive_number(*combat, "hit_radius_units", "combat.hit_radius_units", errors);
  }

  if (const auto* inventory = read_object_field(config, "inventory", "inventory", errors);
      inventory != nullptr) {
    validate_positive_integer(*inventory, "spawn_interval_ticks", "inventory.spawn_interval_ticks", errors);
    validate_positive_integer(*inventory, "max_active_spawns", "inventory.max_active_spawns", errors);
    validate_positive_integer(*inventory, "pickup_queue_capacity", "inventory.pickup_queue_capacity", errors);
    validate_positive_number(*inventory, "pickup_radius_units", "inventory.pickup_radius_units", errors);
    validate_positive_integer(*inventory, "max_items_per_player", "inventory.max_items_per_player", errors);
    validate_positive_integer(*inventory, "max_weight_per_player", "inventory.max_weight_per_player", errors);
    validate_non_negative_number(*inventory, "death_drop_spread_units",
                                 "inventory.death_drop_spread_units", errors);
  }

  const auto match_time_minutes =
      read_integer_field(config, "match_time_minutes", "match_time_minutes", errors);
  if (match_time_minutes.present && match_time_minutes.valid && match_time_minutes.value <= 0) {
    add_error(errors, "match_time_minutes", "must be greater than zero.");
  }

  const auto legacy_respawns = read_integer_field(config, "respawns", "respawns", errors);
  if (legacy_respawns.present && legacy_respawns.valid && legacy_respawns.value < 0) {
    add_error(errors, "respawns", "must be zero or greater.");
  }

  if (const auto* match = read_object_field(config, "match", "match", errors); match != nullptr) {
    validate_non_negative_integer(*match, "pre_match_seconds", "match.pre_match_seconds", errors);
    validate_positive_integer(*match, "duration_seconds", "match.duration_seconds", errors);
    validate_non_negative_integer(*match, "respawns_per_player", "match.respawns_per_player", errors);
    validate_positive_integer(*match, "respawn_delay_seconds", "match.respawn_delay_seconds", errors);
    validate_positive_integer(*match, "min_players_to_start", "match.min_players_to_start", errors);
  }

  if (const auto* map = read_object_field(config, "map", "map", errors); map != nullptr) {
    validate_positive_integer(*map, "chunks_x", "map.chunks_x", errors);
    validate_positive_integer(*map, "chunks_z", "map.chunks_z", errors);
    validate_positive_integer(*map, "world_height", "map.world_height", errors);
    validate_non_negative_integer(*map, "draw_distance_chunks", "map.draw_distance_chunks", errors);

    const auto world_seed = read_integer_field(*map, "world_seed", "map.world_seed", errors);
    if (world_seed.present && world_seed.valid &&
        (world_seed.value < 0 ||
         world_seed.value > static_cast<int64_t>(std::numeric_limits<uint32_t>::max()))) {
      add_error(errors, "map.world_seed", "must be between 0 and 4294967295.");
    }

    if (const auto* world_expansion =
            read_object_field(*map, "world_expansion", "map.world_expansion", errors);
        world_expansion != nullptr) {
      static_cast<void>(read_bool_field(*world_expansion, "enabled", "map.world_expansion.enabled",
                                        errors));

      const auto poi_density =
          read_string_field(*world_expansion, "poi_density", "map.world_expansion.poi_density",
                            errors);
      if (poi_density.present && poi_density.valid &&
          !devy::voxel::parse_poi_density(poi_density.value).has_value()) {
        add_error(errors, "map.world_expansion.poi_density", "must be one of: low, medium, high.");
      }

      if (const auto* placement = read_object_field(*world_expansion, "placement",
                                                    "map.world_expansion.placement", errors);
          placement != nullptr) {
        validate_positive_integer(*placement, "cell_size_chunks",
                                  "map.world_expansion.placement.cell_size_chunks", errors);
        validate_non_negative_integer(*placement, "jitter_units",
                                      "map.world_expansion.placement.jitter_units", errors);
        validate_non_negative_integer(
            *placement, "min_poi_spacing_units",
            "map.world_expansion.placement.min_poi_spacing_units", errors);
      }

      if (const auto* poi_types = read_object_field(*world_expansion, "poi_types",
                                                    "map.world_expansion.poi_types", errors);
          poi_types != nullptr) {
        static_cast<void>(read_bool_field(*poi_types, "outpost",
                                          "map.world_expansion.poi_types.outpost", errors));
        static_cast<void>(read_bool_field(*poi_types, "ruins",
                                          "map.world_expansion.poi_types.ruins", errors));
        static_cast<void>(read_bool_field(*poi_types, "loot_shrine",
                                          "map.world_expansion.poi_types.loot_shrine", errors));
      }

      validate_positive_number(*world_expansion, "loot_bias_multiplier",
                               "map.world_expansion.loot_bias_multiplier", errors);
    }
  }

  return errors;
}

} // namespace devy::server
