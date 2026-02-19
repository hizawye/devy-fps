#include "client/AuthoritativeHudModel.h"

#include <cmath>
#include <cstdint>
#include <limits>
#include <optional>

namespace devy::client {
namespace {

template <typename T>
bool assign_if_changed(T& target, const T& next) {
  if (target == next) {
    return false;
  }
  target = next;
  return true;
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
  }
  return std::nullopt;
}

std::optional<int> json_to_i32(const nlohmann::json& value) {
  if (value.is_number_integer()) {
    const int64_t raw = value.get<int64_t>();
    if (raw < static_cast<int64_t>(std::numeric_limits<int>::min()) ||
        raw > static_cast<int64_t>(std::numeric_limits<int>::max())) {
      return std::nullopt;
    }
    return static_cast<int>(raw);
  }
  if (value.is_number_unsigned()) {
    const uint64_t raw = value.get<uint64_t>();
    if (raw > static_cast<uint64_t>(std::numeric_limits<int>::max())) {
      return std::nullopt;
    }
    return static_cast<int>(raw);
  }
  return std::nullopt;
}

std::optional<int> json_to_floor_seconds(const nlohmann::json& value) {
  if (!value.is_number()) {
    return std::nullopt;
  }
  const double raw = value.get<double>();
  if (!std::isfinite(raw)) {
    return std::nullopt;
  }
  const double floored = std::floor(raw);
  if (floored < static_cast<double>(std::numeric_limits<int>::min()) ||
      floored > static_cast<double>(std::numeric_limits<int>::max())) {
    return std::nullopt;
  }
  return static_cast<int>(floored);
}

} // namespace

void AuthoritativeHudModel::reset() { state_ = {}; }

bool AuthoritativeHudModel::apply_snapshot(const nlohmann::json& snapshot_payload,
                                           uint32_t local_player_id) {
  if (!snapshot_payload.is_object()) {
    return false;
  }

  bool changed = false;
  if (snapshot_payload.contains("players") && snapshot_payload["players"].is_array()) {
    for (const auto& player : snapshot_payload["players"]) {
      if (!player.is_object()) {
        continue;
      }
      const auto player_id = json_to_u32(player.value("player_id", nlohmann::json{}));
      if (!player_id.has_value() || player_id.value() != local_player_id) {
        continue;
      }

      changed |= assign_if_changed(state_.has_player_state, true);
      if (const auto health = json_to_i32(player.value("health", nlohmann::json{}));
          health.has_value()) {
        changed |= assign_if_changed(state_.health, health.value());
      }
      if (player.contains("alive") && player["alive"].is_boolean()) {
        changed |= assign_if_changed(state_.alive, player["alive"].get<bool>());
      }
      if (const auto shot_seq = json_to_u32(player.value("last_shot_seq", nlohmann::json{}));
          shot_seq.has_value()) {
        changed |= assign_if_changed(state_.last_shot_seq, shot_seq.value());
      }
      if (const auto coins = json_to_i32(player.value("coins", nlohmann::json{}));
          coins.has_value()) {
        changed |= assign_if_changed(state_.coins, coins.value());
      }
      if (const auto items = json_to_u32(player.value("inventory_items", nlohmann::json{}));
          items.has_value()) {
        changed |= assign_if_changed(state_.inventory_items, items.value());
      }
      break;
    }
  }

  if (snapshot_payload.contains("match_state") && snapshot_payload["match_state"].is_object()) {
    changed |= apply_match_state(snapshot_payload["match_state"]);
  }

  return changed;
}

bool AuthoritativeHudModel::apply_inventory_update(const nlohmann::json& payload,
                                                   uint32_t local_player_id) {
  if (!payload.is_object()) {
    return false;
  }
  const auto player_id = json_to_u32(payload.value("player_id", nlohmann::json{}));
  if (!player_id.has_value() || player_id.value() != local_player_id) {
    return false;
  }

  bool changed = assign_if_changed(state_.has_player_state, true);
  if (const auto coins = json_to_i32(payload.value("coins", nlohmann::json{})); coins.has_value()) {
    changed |= assign_if_changed(state_.coins, coins.value());
  }
  if (const auto items = json_to_u32(payload.value("item_count", nlohmann::json{}));
      items.has_value()) {
    changed |= assign_if_changed(state_.inventory_items, items.value());
  }
  return changed;
}

bool AuthoritativeHudModel::apply_match_state(const nlohmann::json& payload) {
  if (!payload.is_object()) {
    return false;
  }

  bool changed = false;
  if (payload.contains("state") && payload["state"].is_string()) {
    changed |= assign_if_changed(state_.match_state, payload["state"].get<std::string>());
  }
  if (const auto seconds = json_to_floor_seconds(payload.value("remaining_seconds", nlohmann::json{}));
      seconds.has_value()) {
    changed |= assign_if_changed(state_.match_remaining_seconds, seconds.value());
  }
  return changed;
}

bool AuthoritativeHudModel::apply_damage_event(const nlohmann::json& payload,
                                               uint32_t local_player_id) {
  if (!payload.is_object()) {
    return false;
  }
  const auto victim_id = json_to_u32(payload.value("victim_id", nlohmann::json{}));
  if (!victim_id.has_value() || victim_id.value() != local_player_id) {
    return false;
  }

  bool changed = false;
  if (const auto victim_health = json_to_i32(payload.value("victim_health", nlohmann::json{}));
      victim_health.has_value()) {
    changed |= assign_if_changed(state_.health, victim_health.value());
    changed |= assign_if_changed(state_.has_player_state, true);
  }
  if (payload.contains("lethal") && payload["lethal"].is_boolean()) {
    changed |= assign_if_changed(state_.alive, !payload["lethal"].get<bool>());
  }
  return changed;
}

std::string AuthoritativeHudModel::compose_window_title(const std::string& base_title,
                                                        const std::string& weapon_id) const {
  const std::string hp = state_.has_player_state ? std::to_string(state_.health) : "?";
  const std::string alive =
      state_.has_player_state ? (state_.alive ? "alive" : "down") : "?";
  const std::string shot_seq =
      state_.has_player_state ? std::to_string(state_.last_shot_seq) : "?";
  const std::string coins = state_.has_player_state ? std::to_string(state_.coins) : "?";
  const std::string items =
      state_.has_player_state ? std::to_string(state_.inventory_items) : "?";
  const std::string phase =
      state_.match_state.empty() ? std::string("unknown") : state_.match_state;
  const std::string remaining = state_.match_remaining_seconds >= 0
                                    ? (std::to_string(state_.match_remaining_seconds) + "s")
                                    : "?";
  const std::string weapon = weapon_id.empty() ? std::string("unknown") : weapon_id;

  return base_title + " | HP " + hp + " (" + alive + ")" + " | Weapon " + weapon +
         " | ShotSeq " + shot_seq + " | Coins " + coins + " | Items " + items +
         " | Match " + phase + " " + remaining;
}

const AuthoritativeHudState& AuthoritativeHudModel::state() const { return state_; }

} // namespace devy::client
