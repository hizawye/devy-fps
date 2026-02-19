#pragma once

#include <cstdint>
#include <string>

#include <nlohmann/json.hpp>

namespace devy::client {

struct AuthoritativeHudState {
  bool has_player_state{false};
  int health{0};
  bool alive{true};
  uint32_t last_shot_seq{0U};
  int coins{0};
  uint32_t inventory_items{0U};
  std::string match_state{"unknown"};
  int match_remaining_seconds{-1};
};

class AuthoritativeHudModel {
public:
  void reset();

  bool apply_snapshot(const nlohmann::json& snapshot_payload, uint32_t local_player_id);
  bool apply_inventory_update(const nlohmann::json& payload, uint32_t local_player_id);
  bool apply_match_state(const nlohmann::json& payload);
  bool apply_damage_event(const nlohmann::json& payload, uint32_t local_player_id);

  [[nodiscard]] std::string compose_window_title(const std::string& base_title,
                                                 const std::string& weapon_id) const;
  [[nodiscard]] const AuthoritativeHudState& state() const;

private:
  AuthoritativeHudState state_{};
};

} // namespace devy::client
