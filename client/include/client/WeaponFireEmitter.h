#pragma once

#include "shared/net/Protocol.h"

#include <cstdint>
#include <optional>
#include <string>

namespace devy::client {

enum class WeaponFireEmitStatus : uint8_t {
  Accepted = 0,
  InvalidPlayerId,
  InvalidWeaponId,
  InvalidOrigin,
  InvalidDirection
};

struct WeaponFireEmitResult {
  WeaponFireEmitStatus status{WeaponFireEmitStatus::InvalidPlayerId};
  devy::net::Packet packet{};
  uint32_t shot_seq{0U};

  [[nodiscard]] bool accepted() const {
    return status == WeaponFireEmitStatus::Accepted;
  }
};

class WeaponFireEmitter {
 public:
  explicit WeaponFireEmitter(uint32_t next_shot_seq = 1U);

  void reset(uint32_t next_shot_seq = 1U);
  [[nodiscard]] uint32_t next_shot_seq() const;

  WeaponFireEmitResult emit(uint32_t player_id, const std::string& weapon_id, float origin_x,
                            float origin_y, float direction_x, float direction_y,
                            std::optional<uint64_t> client_fire_time_ms = std::nullopt);

 private:
  uint32_t next_shot_seq_{1U};
};

const char* to_string(WeaponFireEmitStatus status);

} // namespace devy::client
