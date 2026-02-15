#include "server/CombatSimulation.h"

#include "server/MovementSimulation.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdint>
#include <limits>
#include <utility>

namespace devy::server {
namespace {

constexpr int kDefaultStartingHealth = 100;
constexpr float kDefaultHitscanRangeUnits = 128.0F;
constexpr float kDefaultProjectileRangeUnits = 96.0F;
constexpr float kDefaultProjectileSpeedUnitsPerSecond = 40.0F;
constexpr float kDefaultHitRadiusUnits = 0.75F;
constexpr float kDirectionEpsilon = 0.000001F;

} // namespace

CombatSimulation::CombatSimulation(std::vector<devy::game::WeaponDefinition> weapon_definitions,
                                   CombatConfig config)
    : config_(sanitize_config(config)) {
  for (const auto& weapon : weapon_definitions) {
    if (weapon.id.empty()) {
      continue;
    }

    WeaponRuntime runtime{};
    runtime.id = lowercase(weapon.id);
    runtime.weapon_class = classify_weapon(weapon);
    runtime.damage = std::max(0, static_cast<int>(std::lround(devy::game::weapon_damage_per_shot(weapon))));
    if (runtime.damage <= 0) {
      continue;
    }
    runtime.rate_of_fire = std::isfinite(weapon.rate_of_fire) && weapon.rate_of_fire > 0.0F
                               ? weapon.rate_of_fire
                               : 1.0F;

    if (runtime.weapon_class == WeaponClass::Hitscan) {
      runtime.range_units =
          (std::isfinite(weapon.range_units) && weapon.range_units > 0.0F)
              ? weapon.range_units
              : config_.default_hitscan_range_units;
      runtime.projectile_speed_units_per_second = 0.0F;
    } else {
      runtime.range_units =
          (std::isfinite(weapon.range_units) && weapon.range_units > 0.0F)
              ? weapon.range_units
              : config_.default_projectile_range_units;
      runtime.projectile_speed_units_per_second =
          (std::isfinite(weapon.projectile_speed_units_per_second) &&
           weapon.projectile_speed_units_per_second > 0.0F)
              ? weapon.projectile_speed_units_per_second
              : config_.default_projectile_speed_units_per_second;
    }
    weapons_by_id_[runtime.id] = std::move(runtime);
  }
}

void CombatSimulation::reset() {
  players_by_id_.clear();
  positions_by_player_.clear();
  fire_queue_by_player_.clear();
  last_shot_seq_by_player_.clear();
  next_fire_tick_by_player_.clear();
  pending_projectiles_.clear();
}

void CombatSimulation::ensure_player(uint32_t player_id) {
  auto [it, inserted] = players_by_id_.try_emplace(player_id);
  if (inserted) {
    it->second.player_id = player_id;
    it->second.health = config_.starting_health;
    it->second.alive = true;
    it->second.last_shot_seq = 0U;
  }
}

void CombatSimulation::remove_player(uint32_t player_id) {
  players_by_id_.erase(player_id);
  positions_by_player_.erase(player_id);
  fire_queue_by_player_.erase(player_id);
  last_shot_seq_by_player_.erase(player_id);
  next_fire_tick_by_player_.erase(player_id);
  pending_projectiles_.erase(
      std::remove_if(pending_projectiles_.begin(), pending_projectiles_.end(),
                     [player_id](const PendingProjectileHit& hit) {
                       return hit.attacker_id == player_id || hit.victim_id == player_id;
                     }),
      pending_projectiles_.end());
}

void CombatSimulation::respawn_player(uint32_t player_id) {
  ensure_player(player_id);
  auto player_it = players_by_id_.find(player_id);
  if (player_it == players_by_id_.end()) {
    return;
  }
  player_it->second.health = config_.starting_health;
  player_it->second.alive = true;
  pending_projectiles_.erase(
      std::remove_if(pending_projectiles_.begin(), pending_projectiles_.end(),
                     [player_id](const PendingProjectileHit& hit) {
                       return hit.victim_id == player_id;
                     }),
      pending_projectiles_.end());
}

void CombatSimulation::set_player_position(uint32_t player_id, float x, float y) {
  ensure_player(player_id);
  if (!std::isfinite(x) || !std::isfinite(y)) {
    return;
  }
  positions_by_player_[player_id] = std::make_pair(x, y);
}

void CombatSimulation::set_player_positions(const std::vector<PlayerMotionState>& states) {
  for (const auto& state : states) {
    set_player_position(state.player_id, state.position_x, state.position_y);
  }
}

FireEnqueueStatus CombatSimulation::enqueue_fire(const FireCommand& fire) {
  const std::string weapon_id = lowercase(fire.weapon_id);
  if (weapons_by_id_.find(weapon_id) == weapons_by_id_.end()) {
    return FireEnqueueStatus::UnknownWeapon;
  }

  auto last_seq_it = last_shot_seq_by_player_.find(fire.attacker_id);
  if (last_seq_it != last_shot_seq_by_player_.end() && fire.shot_seq <= last_seq_it->second) {
    return FireEnqueueStatus::OutOfOrder;
  }

  auto& queue = fire_queue_by_player_[fire.attacker_id];
  if (!queue.empty() && fire.shot_seq <= queue.back().shot_seq) {
    return FireEnqueueStatus::OutOfOrder;
  }

  FireCommand normalized = fire;
  normalized.weapon_id = weapon_id;
  queue.push_back(std::move(normalized));
  last_shot_seq_by_player_[fire.attacker_id] = fire.shot_seq;
  ensure_player(fire.attacker_id);
  return FireEnqueueStatus::Accepted;
}

CombatTickResult CombatSimulation::resolve_tick(uint64_t tick, std::chrono::nanoseconds tick_interval) {
  CombatTickResult out{};
  resolve_projectiles(tick, out);

  auto queued_fires = drain_fire_commands_deterministic();
  for (const auto& fire : queued_fires) {
    auto attacker_it = players_by_id_.find(fire.command.attacker_id);
    if (attacker_it == players_by_id_.end() || !attacker_it->second.alive) {
      continue;
    }
    if (!fire.weapon) {
      continue;
    }

    const uint64_t next_allowed_tick = next_fire_tick_by_player_[fire.command.attacker_id];
    if (tick < next_allowed_tick) {
      continue;
    }
    next_fire_tick_by_player_[fire.command.attacker_id] =
        tick + cooldown_ticks(fire.weapon->rate_of_fire, tick_interval);
    attacker_it->second.last_shot_seq =
        std::max(attacker_it->second.last_shot_seq, fire.command.shot_seq);

    const auto hit = raycast_victim(fire);
    if (!hit.has_value()) {
      continue;
    }

    if (fire.weapon->weapon_class == WeaponClass::Hitscan) {
      apply_damage(tick,
                   {tick, fire.command.attacker_id, hit->victim_id, fire.command.shot_seq,
                    fire.weapon->id, fire.weapon->damage},
                   out);
      continue;
    }

    const uint64_t resolve_after_ticks = travel_delay_ticks(
        hit->distance_along_ray, fire.weapon->projectile_speed_units_per_second, tick_interval);
    pending_projectiles_.push_back({tick + resolve_after_ticks, fire.command.attacker_id,
                                    hit->victim_id, fire.command.shot_seq, fire.weapon->id,
                                    fire.weapon->damage});
  }

  return out;
}

std::optional<PlayerCombatState> CombatSimulation::state_for(uint32_t player_id) const {
  auto it = players_by_id_.find(player_id);
  if (it == players_by_id_.end()) {
    return std::nullopt;
  }
  return it->second;
}

std::vector<PlayerCombatState> CombatSimulation::snapshot() const {
  std::vector<PlayerCombatState> states{};
  states.reserve(players_by_id_.size());
  for (const auto& [player_id, state] : players_by_id_) {
    static_cast<void>(player_id);
    states.push_back(state);
  }
  std::sort(states.begin(), states.end(),
            [](const PlayerCombatState& lhs, const PlayerCombatState& rhs) {
              return lhs.player_id < rhs.player_id;
            });
  return states;
}

CombatConfig CombatSimulation::sanitize_config(CombatConfig config) {
  if (config.starting_health <= 0) {
    config.starting_health = kDefaultStartingHealth;
  }
  if (!std::isfinite(config.default_hitscan_range_units) || config.default_hitscan_range_units <= 0.0F) {
    config.default_hitscan_range_units = kDefaultHitscanRangeUnits;
  }
  if (!std::isfinite(config.default_projectile_range_units) ||
      config.default_projectile_range_units <= 0.0F) {
    config.default_projectile_range_units = kDefaultProjectileRangeUnits;
  }
  if (!std::isfinite(config.default_projectile_speed_units_per_second) ||
      config.default_projectile_speed_units_per_second <= 0.0F) {
    config.default_projectile_speed_units_per_second = kDefaultProjectileSpeedUnitsPerSecond;
  }
  if (!std::isfinite(config.hit_radius_units) || config.hit_radius_units <= 0.0F) {
    config.hit_radius_units = kDefaultHitRadiusUnits;
  }
  return config;
}

CombatSimulation::WeaponClass CombatSimulation::classify_weapon(
    const devy::game::WeaponDefinition& weapon) {
  return lowercase(weapon.type) == "projectile" ? WeaponClass::Projectile : WeaponClass::Hitscan;
}

std::string CombatSimulation::lowercase(const std::string& value) {
  std::string out = value;
  std::transform(out.begin(), out.end(), out.begin(), [](unsigned char c) {
    return static_cast<char>(std::tolower(c));
  });
  return out;
}

uint64_t CombatSimulation::cooldown_ticks(float rate_of_fire_per_second,
                                          std::chrono::nanoseconds tick_interval) {
  if (!std::isfinite(rate_of_fire_per_second) || rate_of_fire_per_second <= 0.0F ||
      tick_interval <= std::chrono::nanoseconds::zero()) {
    return 1U;
  }

  const double shot_interval_seconds = 1.0 / static_cast<double>(rate_of_fire_per_second);
  const double tick_seconds = static_cast<double>(tick_interval.count()) / 1'000'000'000.0;
  if (tick_seconds <= 0.0) {
    return 1U;
  }

  const double ticks = std::ceil(shot_interval_seconds / tick_seconds);
  if (ticks < 1.0) {
    return 1U;
  }
  if (ticks > static_cast<double>(std::numeric_limits<uint64_t>::max())) {
    return std::numeric_limits<uint64_t>::max();
  }
  return static_cast<uint64_t>(ticks);
}

uint64_t CombatSimulation::travel_delay_ticks(float distance_units, float speed_units_per_second,
                                              std::chrono::nanoseconds tick_interval) {
  if (!std::isfinite(distance_units) || distance_units < 0.0F ||
      !std::isfinite(speed_units_per_second) || speed_units_per_second <= 0.0F ||
      tick_interval <= std::chrono::nanoseconds::zero()) {
    return 1U;
  }

  const double travel_seconds = static_cast<double>(distance_units) / static_cast<double>(speed_units_per_second);
  const double tick_seconds = static_cast<double>(tick_interval.count()) / 1'000'000'000.0;
  if (tick_seconds <= 0.0) {
    return 1U;
  }
  const double ticks = std::ceil(travel_seconds / tick_seconds);
  if (ticks < 1.0) {
    return 1U;
  }
  if (ticks > static_cast<double>(std::numeric_limits<uint64_t>::max())) {
    return std::numeric_limits<uint64_t>::max();
  }
  return static_cast<uint64_t>(ticks);
}

std::vector<CombatSimulation::QueuedFire> CombatSimulation::drain_fire_commands_deterministic() {
  std::vector<uint32_t> player_ids{};
  player_ids.reserve(fire_queue_by_player_.size());
  for (const auto& [player_id, queue] : fire_queue_by_player_) {
    if (!queue.empty()) {
      player_ids.push_back(player_id);
    }
  }
  std::sort(player_ids.begin(), player_ids.end());

  std::vector<QueuedFire> drained{};
  for (uint32_t player_id : player_ids) {
    auto queue_it = fire_queue_by_player_.find(player_id);
    if (queue_it == fire_queue_by_player_.end()) {
      continue;
    }

    auto& queue = queue_it->second;
    while (!queue.empty()) {
      FireCommand fire = queue.front();
      queue.pop_front();
      auto weapon_it = weapons_by_id_.find(fire.weapon_id);
      const WeaponRuntime* weapon = weapon_it != weapons_by_id_.end() ? &weapon_it->second : nullptr;
      drained.push_back({std::move(fire), weapon});
    }

    fire_queue_by_player_.erase(queue_it);
  }
  return drained;
}

std::optional<CombatSimulation::RaycastHit> CombatSimulation::raycast_victim(const QueuedFire& fire) const {
  if (!fire.weapon) {
    return std::nullopt;
  }
  const float dir_length_sq = fire.command.direction_x * fire.command.direction_x +
                              fire.command.direction_y * fire.command.direction_y;
  if (!std::isfinite(dir_length_sq) || dir_length_sq < kDirectionEpsilon) {
    return std::nullopt;
  }

  const float dir_length = std::sqrt(dir_length_sq);
  const float dir_x = fire.command.direction_x / dir_length;
  const float dir_y = fire.command.direction_y / dir_length;
  const float range_units = fire.weapon->range_units;
  const float hit_radius_sq = config_.hit_radius_units * config_.hit_radius_units;

  std::optional<RaycastHit> best_hit{};
  for (const auto& [victim_id, victim_state] : players_by_id_) {
    if (victim_id == fire.command.attacker_id || !victim_state.alive) {
      continue;
    }

    auto position_it = positions_by_player_.find(victim_id);
    if (position_it == positions_by_player_.end()) {
      continue;
    }

    const float rel_x = position_it->second.first - fire.command.origin_x;
    const float rel_y = position_it->second.second - fire.command.origin_y;
    const float along_ray = rel_x * dir_x + rel_y * dir_y;
    if (!std::isfinite(along_ray) || along_ray < 0.0F || along_ray > range_units) {
      continue;
    }

    const float closest_x = fire.command.origin_x + dir_x * along_ray;
    const float closest_y = fire.command.origin_y + dir_y * along_ray;
    const float delta_x = position_it->second.first - closest_x;
    const float delta_y = position_it->second.second - closest_y;
    const float distance_sq = delta_x * delta_x + delta_y * delta_y;
    if (!std::isfinite(distance_sq) || distance_sq > hit_radius_sq) {
      continue;
    }

    if (!best_hit.has_value() || along_ray < best_hit->distance_along_ray ||
        (std::fabs(along_ray - best_hit->distance_along_ray) <= 0.000001F &&
         victim_id < best_hit->victim_id)) {
      best_hit = RaycastHit{victim_id, along_ray};
    }
  }
  return best_hit;
}

void CombatSimulation::apply_damage(uint64_t tick, const PendingProjectileHit& hit, CombatTickResult& out) {
  auto victim_it = players_by_id_.find(hit.victim_id);
  if (victim_it == players_by_id_.end() || !victim_it->second.alive) {
    return;
  }

  const int damage = std::max(0, hit.damage);
  if (damage <= 0) {
    return;
  }

  victim_it->second.health = std::max(0, victim_it->second.health - damage);
  const bool lethal = victim_it->second.health == 0;
  if (lethal) {
    victim_it->second.alive = false;
  }

  out.damage_events.push_back({tick, hit.attacker_id, hit.victim_id, hit.shot_seq, hit.weapon_id,
                               damage, victim_it->second.health, lethal});
  if (lethal) {
    out.death_events.push_back({tick, hit.victim_id, hit.attacker_id, hit.shot_seq, hit.weapon_id});
  }
}

void CombatSimulation::resolve_projectiles(uint64_t tick, CombatTickResult& out) {
  std::vector<PendingProjectileHit> remaining{};
  remaining.reserve(pending_projectiles_.size());

  std::vector<PendingProjectileHit> due{};
  due.reserve(pending_projectiles_.size());
  for (const auto& pending : pending_projectiles_) {
    if (pending.resolve_tick <= tick) {
      due.push_back(pending);
    } else {
      remaining.push_back(pending);
    }
  }
  pending_projectiles_ = std::move(remaining);

  std::sort(due.begin(), due.end(), [](const PendingProjectileHit& lhs, const PendingProjectileHit& rhs) {
    if (lhs.resolve_tick != rhs.resolve_tick) {
      return lhs.resolve_tick < rhs.resolve_tick;
    }
    if (lhs.attacker_id != rhs.attacker_id) {
      return lhs.attacker_id < rhs.attacker_id;
    }
    if (lhs.shot_seq != rhs.shot_seq) {
      return lhs.shot_seq < rhs.shot_seq;
    }
    return lhs.victim_id < rhs.victim_id;
  });

  for (const auto& hit : due) {
    apply_damage(tick, hit, out);
  }
}

const char* to_string(FireEnqueueStatus status) {
  switch (status) {
    case FireEnqueueStatus::Accepted: return "accepted";
    case FireEnqueueStatus::UnknownWeapon: return "unknown_weapon";
    case FireEnqueueStatus::OutOfOrder: return "out_of_order";
    default: return "unknown_fire_enqueue_status";
  }
}

} // namespace devy::server
