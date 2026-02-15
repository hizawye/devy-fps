#include "client/WeaponFireEmitter.h"

#include <algorithm>
#include <cmath>

namespace devy::client {
namespace {

constexpr float kDirectionEpsilon = 0.000001F;

bool is_finite(float value) {
  return std::isfinite(value);
}

} // namespace

WeaponFireEmitter::WeaponFireEmitter(uint32_t next_shot_seq) {
  reset(next_shot_seq);
}

void WeaponFireEmitter::reset(uint32_t next_shot_seq) {
  next_shot_seq_ = std::max(1U, next_shot_seq);
}

uint32_t WeaponFireEmitter::next_shot_seq() const { return next_shot_seq_; }

WeaponFireEmitResult WeaponFireEmitter::emit(uint32_t player_id, const std::string& weapon_id,
                                             float origin_x, float origin_y, float direction_x,
                                             float direction_y,
                                             std::optional<uint64_t> client_fire_time_ms) {
  WeaponFireEmitResult result{};
  if (player_id == 0U) {
    result.status = WeaponFireEmitStatus::InvalidPlayerId;
    return result;
  }
  if (weapon_id.empty()) {
    result.status = WeaponFireEmitStatus::InvalidWeaponId;
    return result;
  }
  if (!is_finite(origin_x) || !is_finite(origin_y)) {
    result.status = WeaponFireEmitStatus::InvalidOrigin;
    return result;
  }
  if (!is_finite(direction_x) || !is_finite(direction_y)) {
    result.status = WeaponFireEmitStatus::InvalidDirection;
    return result;
  }

  const float direction_length_sq = direction_x * direction_x + direction_y * direction_y;
  if (direction_length_sq < kDirectionEpsilon) {
    result.status = WeaponFireEmitStatus::InvalidDirection;
    return result;
  }

  const float direction_length = std::sqrt(direction_length_sq);
  const float normalized_direction_x = direction_x / direction_length;
  const float normalized_direction_y = direction_y / direction_length;

  result.status = WeaponFireEmitStatus::Accepted;
  result.shot_seq = next_shot_seq_++;
  result.packet.type = devy::net::MessageType::WeaponFire;
  result.packet.version = devy::net::kProtocolVersion;
  result.packet.payload = {{"player_id", player_id},
                           {"shot_seq", result.shot_seq},
                           {"weapon_id", weapon_id},
                           {"origin", {{"x", origin_x}, {"y", origin_y}}},
                           {"direction",
                            {{"x", normalized_direction_x}, {"y", normalized_direction_y}}}};
  if (client_fire_time_ms.has_value()) {
    result.packet.payload["client_fire_time_ms"] = client_fire_time_ms.value();
  }
  return result;
}

const char* to_string(WeaponFireEmitStatus status) {
  switch (status) {
  case WeaponFireEmitStatus::Accepted:
    return "accepted";
  case WeaponFireEmitStatus::InvalidPlayerId:
    return "invalid_player_id";
  case WeaponFireEmitStatus::InvalidWeaponId:
    return "invalid_weapon_id";
  case WeaponFireEmitStatus::InvalidOrigin:
    return "invalid_origin";
  case WeaponFireEmitStatus::InvalidDirection:
    return "invalid_direction";
  default:
    return "unknown_weapon_fire_emit_status";
  }
}

} // namespace devy::client
