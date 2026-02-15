#pragma once

#include "server/AuthoritativeLoop.h"
#include "server/MovementSimulation.h"
#include "shared/game/Weapons.h"

#include <chrono>
#include <cstdint>
#include <deque>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

namespace devy::server {

struct CombatConfig {
  int starting_health{100};
  float default_hitscan_range_units{128.0F};
  float default_projectile_range_units{96.0F};
  float default_projectile_speed_units_per_second{40.0F};
  float hit_radius_units{0.75F};
};

struct FireCommand {
  uint32_t attacker_id{0};
  uint32_t shot_seq{0};
  std::string weapon_id{};
  float origin_x{0.0F};
  float origin_y{0.0F};
  float direction_x{0.0F};
  float direction_y{0.0F};
  RuntimeTimePoint received_at{};
};

enum class FireEnqueueStatus : uint8_t {
  Accepted = 0,
  UnknownWeapon,
  OutOfOrder
};

struct DamageResolution {
  uint64_t tick{0};
  uint32_t attacker_id{0};
  uint32_t victim_id{0};
  uint32_t shot_seq{0};
  std::string weapon_id{};
  int damage{0};
  int victim_health{0};
  bool lethal{false};
};

struct DeathResolution {
  uint64_t tick{0};
  uint32_t victim_id{0};
  uint32_t killer_id{0};
  uint32_t shot_seq{0};
  std::string weapon_id{};
};

struct CombatTickResult {
  std::vector<DamageResolution> damage_events{};
  std::vector<DeathResolution> death_events{};
};

struct PlayerCombatState {
  uint32_t player_id{0};
  int health{100};
  bool alive{true};
  uint32_t last_shot_seq{0};
};

class CombatSimulation {
 public:
  explicit CombatSimulation(std::vector<devy::game::WeaponDefinition> weapon_definitions,
                            CombatConfig config = {});

  void reset();
  void ensure_player(uint32_t player_id);
  void remove_player(uint32_t player_id);
  void respawn_player(uint32_t player_id);
  void set_player_position(uint32_t player_id, float x, float y);
  void set_player_positions(const std::vector<PlayerMotionState>& states);
  FireEnqueueStatus enqueue_fire(const FireCommand& fire);
  CombatTickResult resolve_tick(uint64_t tick, std::chrono::nanoseconds tick_interval);

  [[nodiscard]] std::optional<PlayerCombatState> state_for(uint32_t player_id) const;
  [[nodiscard]] std::vector<PlayerCombatState> snapshot() const;

 private:
  enum class WeaponClass : uint8_t {
    Hitscan = 0,
    Projectile
  };

  struct WeaponRuntime {
    std::string id{};
    WeaponClass weapon_class{WeaponClass::Hitscan};
    int damage{0};
    float rate_of_fire{1.0F};
    float range_units{0.0F};
    float projectile_speed_units_per_second{0.0F};
  };

  struct QueuedFire {
    FireCommand command{};
    const WeaponRuntime* weapon{nullptr};
  };

  struct PendingProjectileHit {
    uint64_t resolve_tick{0};
    uint32_t attacker_id{0};
    uint32_t victim_id{0};
    uint32_t shot_seq{0};
    std::string weapon_id{};
    int damage{0};
  };

  struct RaycastHit {
    uint32_t victim_id{0};
    float distance_along_ray{0.0F};
  };

  [[nodiscard]] static CombatConfig sanitize_config(CombatConfig config);
  [[nodiscard]] static WeaponClass classify_weapon(const devy::game::WeaponDefinition& weapon);
  [[nodiscard]] static std::string lowercase(const std::string& value);
  [[nodiscard]] static uint64_t cooldown_ticks(float rate_of_fire_per_second,
                                               std::chrono::nanoseconds tick_interval);
  [[nodiscard]] static uint64_t travel_delay_ticks(float distance_units, float speed_units_per_second,
                                                   std::chrono::nanoseconds tick_interval);

  [[nodiscard]] std::vector<QueuedFire> drain_fire_commands_deterministic();
  [[nodiscard]] std::optional<RaycastHit> raycast_victim(const QueuedFire& fire) const;
  void apply_damage(uint64_t tick, const PendingProjectileHit& hit, CombatTickResult& out);
  void resolve_projectiles(uint64_t tick, CombatTickResult& out);

  CombatConfig config_{};
  std::unordered_map<std::string, WeaponRuntime> weapons_by_id_{};
  std::unordered_map<uint32_t, PlayerCombatState> players_by_id_{};
  std::unordered_map<uint32_t, std::pair<float, float>> positions_by_player_{};
  std::unordered_map<uint32_t, std::deque<FireCommand>> fire_queue_by_player_{};
  std::unordered_map<uint32_t, uint32_t> last_shot_seq_by_player_{};
  std::unordered_map<uint32_t, uint64_t> next_fire_tick_by_player_{};
  std::vector<PendingProjectileHit> pending_projectiles_{};
};

const char* to_string(FireEnqueueStatus status);

} // namespace devy::server
