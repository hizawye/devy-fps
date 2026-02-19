#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

#include <nlohmann/json.hpp>

namespace devy::client {

class AuthoritativeEventFeed {
public:
  void reset();

  [[nodiscard]] std::vector<std::string> consume_snapshot_events(
      const nlohmann::json& snapshot_payload, uint32_t local_player_id);
  [[nodiscard]] std::optional<std::string>
  consume_reliable_damage_event(const nlohmann::json& payload, uint32_t local_player_id);
  [[nodiscard]] std::optional<std::string>
  consume_reliable_match_state(const nlohmann::json& payload);

private:
  struct DamageSignature {
    uint64_t tick{0U};
    uint32_t attacker_id{0U};
    uint32_t victim_id{0U};
    uint32_t shot_seq{0U};
  };

  [[nodiscard]] std::optional<std::string> consume_damage_event(
      const nlohmann::json& payload, uint32_t local_player_id);
  [[nodiscard]] bool is_duplicate_damage(const DamageSignature& signature) const;
  void remember_damage(const DamageSignature& signature);

  std::string last_match_state_{"unknown"};
  std::vector<DamageSignature> recent_damage_signatures_{};
};

} // namespace devy::client
