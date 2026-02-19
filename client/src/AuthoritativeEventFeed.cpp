#include "client/AuthoritativeEventFeed.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <limits>

namespace devy::client {
namespace {

constexpr std::size_t kMaxDamageSignatures = 64U;

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
    if (raw >= 0) {
      return static_cast<uint64_t>(raw);
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

std::string pickup_status_message(const nlohmann::json& event_payload) {
  const std::string status = event_payload.value("status", std::string("unknown"));
  if (status == "collected") {
    const std::string treasure_id = event_payload.value("treasure_id", std::string("treasure"));
    const int total_value = event_payload.value("total_value", 0);
    const int item_count = event_payload.value("item_count", 0);
    return "Pickup collected: " + treasure_id + " | Coins " + std::to_string(total_value) +
           " | Items " + std::to_string(item_count);
  }
  if (status == "inventory_capacity_exceeded") {
    return "Pickup failed: inventory full";
  }
  if (status == "weight_limit_exceeded") {
    return "Pickup failed: over weight limit";
  }
  if (status == "out_of_range") {
    return "Pickup failed: out of range";
  }
  if (status == "unknown_spawn") {
    return "Pickup failed: spawn unavailable";
  }
  if (status == "duplicate_pickup") {
    return "Pickup failed: already collected";
  }
  if (status == "player_dead") {
    return "Pickup failed: player is down";
  }
  if (status == "player_missing") {
    return "Pickup failed: player unavailable";
  }
  return "Pickup failed: " + status;
}

} // namespace

void AuthoritativeEventFeed::reset() {
  last_match_state_ = "unknown";
  recent_damage_signatures_.clear();
}

std::vector<std::string>
AuthoritativeEventFeed::consume_snapshot_events(const nlohmann::json& snapshot_payload,
                                                uint32_t local_player_id) {
  std::vector<std::string> out{};
  if (!snapshot_payload.is_object() || !snapshot_payload.contains("events") ||
      !snapshot_payload["events"].is_array()) {
    return out;
  }

  for (const auto& event_payload : snapshot_payload["events"]) {
    if (!event_payload.is_object() || !event_payload.contains("type") ||
        !event_payload["type"].is_string()) {
      continue;
    }
    const std::string event_type = event_payload["type"].get<std::string>();
    if (event_type == "damage_event") {
      if (const auto message = consume_damage_event(event_payload, local_player_id);
          message.has_value()) {
        out.push_back(message.value());
      }
      continue;
    }
    if (event_type == "death_event") {
      const auto victim_id = json_to_u32(event_payload.value("victim_id", nlohmann::json{}));
      const auto killer_id = json_to_u32(event_payload.value("killer_id", nlohmann::json{}));
      if (victim_id.has_value() && victim_id.value() == local_player_id) {
        out.push_back("Eliminated");
      } else if (killer_id.has_value() && killer_id.value() == local_player_id) {
        out.push_back("Elimination confirmed");
      }
      continue;
    }
    if (event_type == "treasure_pickup") {
      const auto player_id = json_to_u32(event_payload.value("player_id", nlohmann::json{}));
      if (player_id.has_value() && player_id.value() == local_player_id) {
        out.push_back(pickup_status_message(event_payload));
      }
      continue;
    }
    if (event_type == "respawn_event") {
      const auto player_id = json_to_u32(event_payload.value("player_id", nlohmann::json{}));
      if (player_id.has_value() && player_id.value() == local_player_id) {
        out.push_back("Respawned");
      }
      continue;
    }
    if (event_type == "match_state_changed") {
      const std::string state = event_payload.value("state", std::string("unknown"));
      const auto remaining_seconds =
          json_to_floor_seconds(event_payload.value("remaining_seconds", nlohmann::json{}));
      if (state != last_match_state_) {
        last_match_state_ = state;
        if (remaining_seconds.has_value()) {
          out.push_back("Phase: " + state + " (" + std::to_string(remaining_seconds.value()) +
                        "s)");
        } else {
          out.push_back("Phase: " + state);
        }
      }
      continue;
    }
  }
  return out;
}

std::optional<std::string>
AuthoritativeEventFeed::consume_reliable_damage_event(const nlohmann::json& payload,
                                                      uint32_t local_player_id) {
  return consume_damage_event(payload, local_player_id);
}

std::optional<std::string>
AuthoritativeEventFeed::consume_reliable_match_state(const nlohmann::json& payload) {
  if (!payload.is_object()) {
    return std::nullopt;
  }
  const std::string state = payload.value("state", std::string("unknown"));
  if (state == last_match_state_) {
    return std::nullopt;
  }
  last_match_state_ = state;
  const auto remaining_seconds =
      json_to_floor_seconds(payload.value("remaining_seconds", nlohmann::json{}));
  if (remaining_seconds.has_value()) {
    return "Phase: " + state + " (" + std::to_string(remaining_seconds.value()) + "s)";
  }
  return "Phase: " + state;
}

std::optional<std::string>
AuthoritativeEventFeed::consume_damage_event(const nlohmann::json& payload,
                                             uint32_t local_player_id) {
  if (!payload.is_object()) {
    return std::nullopt;
  }
  const auto attacker_id = json_to_u32(payload.value("attacker_id", nlohmann::json{}));
  const auto victim_id = json_to_u32(payload.value("victim_id", nlohmann::json{}));
  const auto damage = json_to_i32(payload.value("damage", nlohmann::json{}));
  if (!attacker_id.has_value() || !victim_id.has_value() || !damage.has_value()) {
    return std::nullopt;
  }

  const auto shot_seq = json_to_u32(payload.value("shot_seq", nlohmann::json{}));
  const auto tick = json_to_u64(payload.value("tick", nlohmann::json{}));
  if (shot_seq.has_value() && tick.has_value()) {
    const DamageSignature signature{tick.value(), attacker_id.value(), victim_id.value(),
                                    shot_seq.value()};
    if (is_duplicate_damage(signature)) {
      return std::nullopt;
    }
    remember_damage(signature);
  }

  const bool lethal = payload.value("lethal", false);
  if (attacker_id.value() == local_player_id && victim_id.value() != local_player_id) {
    return lethal ? "Elimination confirmed" : ("Hit confirmed: " + std::to_string(damage.value()));
  }
  if (victim_id.value() == local_player_id) {
    return lethal ? "You were eliminated"
                  : ("Damage taken: " + std::to_string(damage.value()));
  }
  return std::nullopt;
}

bool AuthoritativeEventFeed::is_duplicate_damage(const DamageSignature& signature) const {
  return std::any_of(recent_damage_signatures_.begin(), recent_damage_signatures_.end(),
                     [&signature](const DamageSignature& known) {
                       return known.tick == signature.tick &&
                              known.attacker_id == signature.attacker_id &&
                              known.victim_id == signature.victim_id &&
                              known.shot_seq == signature.shot_seq;
                     });
}

void AuthoritativeEventFeed::remember_damage(const DamageSignature& signature) {
  recent_damage_signatures_.push_back(signature);
  if (recent_damage_signatures_.size() > kMaxDamageSignatures) {
    recent_damage_signatures_.erase(recent_damage_signatures_.begin());
  }
}

} // namespace devy::client
