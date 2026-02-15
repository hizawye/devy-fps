#include "server/InventoryLootSimulation.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstddef>
#include <limits>
#include <string>
#include <unordered_set>
#include <utility>

namespace devy::server {
namespace {

constexpr uint32_t kDefaultSpawnIntervalTicks = 90U;
constexpr std::size_t kDefaultMaxActiveSpawns = 64U;
constexpr std::size_t kDefaultPickupQueueCapacity = 2048U;
constexpr float kDefaultPickupRadiusUnits = 2.5F;
constexpr std::size_t kDefaultMaxItemsPerPlayer = 16U;
constexpr int kDefaultMaxWeightPerPlayer = 40;
constexpr int kDefaultWorldSizeUnits = 2048;
constexpr float kDefaultDeathDropSpreadUnits = 0.75F;
constexpr float kDistanceEpsilon = 0.000001F;

float clamp_position(float value, int bound) {
  if (!std::isfinite(value)) {
    return 0.0F;
  }
  const float max_value = static_cast<float>(std::max(1, bound)) - 0.001F;
  if (max_value <= 0.0F) {
    return 0.0F;
  }
  return std::clamp(value, 0.0F, max_value);
}

std::pair<float, float> deterministic_spawn_position(uint64_t seed, int world_size_x, int world_size_y) {
  const uint64_t width = static_cast<uint64_t>(std::max(1, world_size_x));
  const uint64_t height = static_cast<uint64_t>(std::max(1, world_size_y));
  const float position_x = static_cast<float>((seed * 37U + 17U) % width) + 0.5F;
  const float position_y = static_cast<float>((seed * 53U + 29U) % height) + 0.5F;
  return {position_x, position_y};
}

std::string lowercase_string(const std::string& value) {
  std::string out = value;
  std::transform(out.begin(), out.end(), out.begin(), [](unsigned char c) {
    return static_cast<char>(std::tolower(c));
  });
  return out;
}

} // namespace

InventoryLootSimulation::InventoryLootSimulation(
    std::vector<devy::game::TreasureDefinition> treasure_definitions, InventoryLootConfig config)
    : treasure_definitions_(std::move(treasure_definitions)), config_(sanitize_config(config)) {
  treasure_definitions_.erase(
      std::remove_if(treasure_definitions_.begin(), treasure_definitions_.end(),
                     [](const devy::game::TreasureDefinition& definition) {
                       return definition.id.empty() || definition.value < 0 || definition.weight < 0;
                     }),
      treasure_definitions_.end());
}

void InventoryLootSimulation::reset() {
  pickup_queue_by_player_.clear();
  active_spawns_by_id_.clear();
  consumed_spawn_ids_.clear();
  next_spawn_id_ = 1U;
  next_spawn_definition_index_ = 0U;
  queued_pickup_count_ = 0U;

  for (auto& [player_id, state] : players_by_id_) {
    static_cast<void>(player_id);
    state.total_value = 0;
    state.total_weight = 0;
    state.last_pickup_seq = 0U;
    state.last_enqueued_pickup_seq = 0U;
    state.items.clear();
  }
}

void InventoryLootSimulation::ensure_player(uint32_t player_id) {
  auto [it, inserted] = players_by_id_.try_emplace(player_id);
  if (inserted) {
    it->second.player_id = player_id;
  }
}

void InventoryLootSimulation::remove_player(uint32_t player_id) {
  auto queue_it = pickup_queue_by_player_.find(player_id);
  if (queue_it != pickup_queue_by_player_.end()) {
    if (queued_pickup_count_ >= queue_it->second.size()) {
      queued_pickup_count_ -= queue_it->second.size();
    } else {
      queued_pickup_count_ = 0U;
    }
    pickup_queue_by_player_.erase(queue_it);
  }
  auto player_it = players_by_id_.find(player_id);
  if (player_it != players_by_id_.end()) {
    player_it->second.alive = false;
    player_it->second.position_x = 0.0F;
    player_it->second.position_y = 0.0F;
  }
}

TreasurePickupEnqueueStatus InventoryLootSimulation::enqueue_pickup(const TreasurePickupCommand& command) {
  ensure_player(command.player_id);
  auto state_it = players_by_id_.find(command.player_id);
  if (state_it == players_by_id_.end()) {
    return TreasurePickupEnqueueStatus::OutOfOrder;
  }

  PlayerInventoryState& state = state_it->second;
  if (command.pickup_seq <= state.last_enqueued_pickup_seq) {
    return TreasurePickupEnqueueStatus::OutOfOrder;
  }
  if (queued_pickup_count_ >= config_.pickup_queue_capacity) {
    return TreasurePickupEnqueueStatus::QueueFull;
  }

  auto& queue = pickup_queue_by_player_[command.player_id];
  if (!queue.empty() && command.pickup_seq <= queue.back().pickup_seq) {
    return TreasurePickupEnqueueStatus::OutOfOrder;
  }

  queue.push_back(command);
  state.last_enqueued_pickup_seq = command.pickup_seq;
  ++queued_pickup_count_;
  return TreasurePickupEnqueueStatus::Accepted;
}

InventoryLootTickResult InventoryLootSimulation::resolve_tick(
    uint64_t tick, const std::vector<PlayerMotionState>& movement_states,
    const std::vector<PlayerCombatState>& combat_states, const std::vector<DeathResolution>& deaths) {
  InventoryLootTickResult out{};
  std::unordered_set<uint32_t> changed_players{};

  for (const auto& movement_state : movement_states) {
    ensure_player(movement_state.player_id);
    auto player_it = players_by_id_.find(movement_state.player_id);
    if (player_it == players_by_id_.end()) {
      continue;
    }
    player_it->second.position_x = movement_state.position_x;
    player_it->second.position_y = movement_state.position_y;
  }

  for (const auto& combat_state : combat_states) {
    ensure_player(combat_state.player_id);
    auto player_it = players_by_id_.find(combat_state.player_id);
    if (player_it == players_by_id_.end()) {
      continue;
    }
    player_it->second.alive = combat_state.alive;
  }

  if (!treasure_definitions_.empty() && config_.spawn_interval_ticks > 0U &&
      tick % config_.spawn_interval_ticks == 0U &&
      active_spawns_by_id_.size() < config_.max_active_spawns) {
    TreasureSpawnInstance spawn = create_scheduled_spawn(tick);
    active_spawns_by_id_[spawn.spawn_id] = spawn;
    out.spawned.push_back(std::move(spawn));
  }

  if (config_.loot_drop_mode == LootDropMode::All && !deaths.empty()) {
    std::vector<DeathResolution> sorted_deaths = deaths;
    if (!is_sorted_player_id(sorted_deaths)) {
      std::sort(sorted_deaths.begin(), sorted_deaths.end(),
                [](const DeathResolution& lhs, const DeathResolution& rhs) {
                  if (lhs.victim_id != rhs.victim_id) {
                    return lhs.victim_id < rhs.victim_id;
                  }
                  if (lhs.killer_id != rhs.killer_id) {
                    return lhs.killer_id < rhs.killer_id;
                  }
                  return lhs.shot_seq < rhs.shot_seq;
                });
    }

    for (const auto& death : sorted_deaths) {
      auto player_it = players_by_id_.find(death.victim_id);
      if (player_it == players_by_id_.end() || player_it->second.items.empty()) {
        continue;
      }

      const float origin_x = player_it->second.position_x;
      const float origin_y = player_it->second.position_y;

      for (std::size_t item_index = 0; item_index < player_it->second.items.size(); ++item_index) {
        const auto& item = player_it->second.items[item_index];
        TreasureSpawnInstance spawn =
            create_death_drop_spawn(tick, item.treasure, origin_x, origin_y, item_index);
        active_spawns_by_id_[spawn.spawn_id] = spawn;
        out.spawned.push_back(std::move(spawn));
      }

      player_it->second.items.clear();
      player_it->second.total_value = 0;
      player_it->second.total_weight = 0;
      append_inventory_delta(death.victim_id, changed_players);
    }
  }

  const auto queued_pickups = drain_pickups_deterministic();
  for (const auto& pickup : queued_pickups) {
    TreasurePickupResolution resolution{};
    resolution.player_id = pickup.player_id;
    resolution.pickup_seq = pickup.pickup_seq;
    resolution.spawn_id = pickup.spawn_id;

    auto player_it = players_by_id_.find(pickup.player_id);
    if (player_it == players_by_id_.end()) {
      resolution.status = TreasurePickupResolveStatus::PlayerMissing;
      out.pickup_results.push_back(std::move(resolution));
      continue;
    }

    PlayerInventoryState& player_state = player_it->second;
    player_state.last_pickup_seq = std::max(player_state.last_pickup_seq, pickup.pickup_seq);

    if (!player_state.alive) {
      resolution.status = TreasurePickupResolveStatus::PlayerDead;
      resolution.total_value = player_state.total_value;
      resolution.total_weight = player_state.total_weight;
      resolution.item_count = static_cast<uint32_t>(player_state.items.size());
      out.pickup_results.push_back(std::move(resolution));
      continue;
    }

    if (consumed_spawn_ids_.find(pickup.spawn_id) != consumed_spawn_ids_.end()) {
      resolution.status = TreasurePickupResolveStatus::DuplicatePickup;
      resolution.total_value = player_state.total_value;
      resolution.total_weight = player_state.total_weight;
      resolution.item_count = static_cast<uint32_t>(player_state.items.size());
      out.pickup_results.push_back(std::move(resolution));
      continue;
    }

    auto spawn_it = active_spawns_by_id_.find(pickup.spawn_id);
    if (spawn_it == active_spawns_by_id_.end()) {
      resolution.status = TreasurePickupResolveStatus::UnknownSpawn;
      resolution.total_value = player_state.total_value;
      resolution.total_weight = player_state.total_weight;
      resolution.item_count = static_cast<uint32_t>(player_state.items.size());
      out.pickup_results.push_back(std::move(resolution));
      continue;
    }

    const TreasureSpawnInstance& spawn = spawn_it->second;
    const float delta_x = spawn.position_x - player_state.position_x;
    const float delta_y = spawn.position_y - player_state.position_y;
    const float distance_sq = delta_x * delta_x + delta_y * delta_y;
    const float radius_sq = config_.pickup_radius_units * config_.pickup_radius_units;
    if (!std::isfinite(distance_sq) || distance_sq > radius_sq + kDistanceEpsilon) {
      resolution.status = TreasurePickupResolveStatus::OutOfRange;
      resolution.total_value = player_state.total_value;
      resolution.total_weight = player_state.total_weight;
      resolution.item_count = static_cast<uint32_t>(player_state.items.size());
      out.pickup_results.push_back(std::move(resolution));
      continue;
    }

    if (player_state.items.size() + 1U > config_.max_items_per_player) {
      resolution.status = TreasurePickupResolveStatus::InventoryCapacityExceeded;
      resolution.total_value = player_state.total_value;
      resolution.total_weight = player_state.total_weight;
      resolution.item_count = static_cast<uint32_t>(player_state.items.size());
      out.pickup_results.push_back(std::move(resolution));
      continue;
    }

    if (player_state.total_weight + spawn.weight > config_.max_weight_per_player) {
      resolution.status = TreasurePickupResolveStatus::WeightLimitExceeded;
      resolution.total_value = player_state.total_value;
      resolution.total_weight = player_state.total_weight;
      resolution.item_count = static_cast<uint32_t>(player_state.items.size());
      out.pickup_results.push_back(std::move(resolution));
      continue;
    }

    devy::game::TreasureDefinition treasure{};
    treasure.id = spawn.treasure_id;
    treasure.value = spawn.value;
    treasure.weight = spawn.weight;
    treasure.rarity = "";
    player_state.items.push_back({spawn.spawn_id, treasure});
    player_state.total_value += spawn.value;
    player_state.total_weight += spawn.weight;
    consumed_spawn_ids_.insert(spawn.spawn_id);
    active_spawns_by_id_.erase(spawn_it);

    resolution.status = TreasurePickupResolveStatus::Collected;
    resolution.treasure_id = treasure.id;
    resolution.total_value = player_state.total_value;
    resolution.total_weight = player_state.total_weight;
    resolution.item_count = static_cast<uint32_t>(player_state.items.size());
    out.pickup_results.push_back(std::move(resolution));

    append_inventory_delta(player_state.player_id, changed_players);
  }

  std::vector<uint32_t> sorted_changed_players{};
  sorted_changed_players.reserve(changed_players.size());
  for (uint32_t player_id : changed_players) {
    sorted_changed_players.push_back(player_id);
  }
  std::sort(sorted_changed_players.begin(), sorted_changed_players.end());
  for (uint32_t player_id : sorted_changed_players) {
    const auto summary = summary_for(player_id);
    if (summary.has_value()) {
      out.inventory_deltas.push_back(summary.value());
    }
  }

  return out;
}

std::optional<InventorySummary> InventoryLootSimulation::summary_for(uint32_t player_id) const {
  auto player_it = players_by_id_.find(player_id);
  if (player_it == players_by_id_.end()) {
    return std::nullopt;
  }
  return to_summary(player_it->second);
}

std::vector<InventorySummary> InventoryLootSimulation::inventory_snapshot() const {
  std::vector<InventorySummary> out{};
  out.reserve(players_by_id_.size());
  for (const auto& [player_id, state] : players_by_id_) {
    static_cast<void>(player_id);
    out.push_back(to_summary(state));
  }
  std::sort(out.begin(), out.end(), [](const InventorySummary& lhs, const InventorySummary& rhs) {
    return lhs.player_id < rhs.player_id;
  });
  return out;
}

std::vector<TreasureSpawnInstance> InventoryLootSimulation::active_spawns() const {
  std::vector<TreasureSpawnInstance> out{};
  out.reserve(active_spawns_by_id_.size());
  for (const auto& [spawn_id, spawn] : active_spawns_by_id_) {
    static_cast<void>(spawn_id);
    out.push_back(spawn);
  }
  std::sort(out.begin(), out.end(),
            [](const TreasureSpawnInstance& lhs, const TreasureSpawnInstance& rhs) {
              return lhs.spawn_id < rhs.spawn_id;
            });
  return out;
}

InventoryLootConfig InventoryLootSimulation::sanitize_config(InventoryLootConfig config) {
  if (config.spawn_interval_ticks == 0U) {
    config.spawn_interval_ticks = kDefaultSpawnIntervalTicks;
  }
  if (config.max_active_spawns == 0U) {
    config.max_active_spawns = kDefaultMaxActiveSpawns;
  }
  if (config.pickup_queue_capacity == 0U) {
    config.pickup_queue_capacity = kDefaultPickupQueueCapacity;
  }
  if (!std::isfinite(config.pickup_radius_units) || config.pickup_radius_units <= 0.0F) {
    config.pickup_radius_units = kDefaultPickupRadiusUnits;
  }
  if (config.max_items_per_player == 0U) {
    config.max_items_per_player = kDefaultMaxItemsPerPlayer;
  }
  if (config.max_weight_per_player <= 0) {
    config.max_weight_per_player = kDefaultMaxWeightPerPlayer;
  }
  if (config.world_size_x_units <= 0) {
    config.world_size_x_units = kDefaultWorldSizeUnits;
  }
  if (config.world_size_y_units <= 0) {
    config.world_size_y_units = kDefaultWorldSizeUnits;
  }
  if (!std::isfinite(config.death_drop_spread_units) || config.death_drop_spread_units < 0.0F) {
    config.death_drop_spread_units = kDefaultDeathDropSpreadUnits;
  }
  return config;
}

bool InventoryLootSimulation::is_sorted_player_id(const std::vector<DeathResolution>& deaths) {
  if (deaths.empty()) {
    return true;
  }
  for (std::size_t i = 1; i < deaths.size(); ++i) {
    if (deaths[i - 1].victim_id > deaths[i].victim_id) {
      return false;
    }
  }
  return true;
}

TreasureSpawnInstance InventoryLootSimulation::create_scheduled_spawn(uint64_t tick) {
  TreasureSpawnInstance spawn{};
  const uint64_t spawn_id = next_spawn_id_++;
  const std::size_t definition_index = next_spawn_definition_index_++ % treasure_definitions_.size();
  const auto& definition = treasure_definitions_[definition_index];
  const auto [spawn_x, spawn_y] =
      deterministic_spawn_position(spawn_id, config_.world_size_x_units, config_.world_size_y_units);

  spawn.spawn_id = spawn_id;
  spawn.treasure_id = definition.id;
  spawn.value = definition.value;
  spawn.weight = definition.weight;
  spawn.position_x = clamp_position(spawn_x, config_.world_size_x_units);
  spawn.position_y = clamp_position(spawn_y, config_.world_size_y_units);
  spawn.source = TreasureSpawnSource::Scheduled;
  spawn.spawned_tick = tick;
  return spawn;
}

TreasureSpawnInstance InventoryLootSimulation::create_death_drop_spawn(
    uint64_t tick, const devy::game::TreasureDefinition& treasure, float origin_x, float origin_y,
    std::size_t item_index) {
  TreasureSpawnInstance spawn{};
  const uint64_t spawn_id = next_spawn_id_++;

  const int offset_x_bucket = static_cast<int>(item_index % 3U) - 1;
  const int offset_y_bucket = static_cast<int>((item_index / 3U) % 3U) - 1;
  const float offset_x = static_cast<float>(offset_x_bucket) * config_.death_drop_spread_units;
  const float offset_y = static_cast<float>(offset_y_bucket) * config_.death_drop_spread_units;

  spawn.spawn_id = spawn_id;
  spawn.treasure_id = treasure.id;
  spawn.value = treasure.value;
  spawn.weight = treasure.weight;
  spawn.position_x = clamp_position(origin_x + offset_x, config_.world_size_x_units);
  spawn.position_y = clamp_position(origin_y + offset_y, config_.world_size_y_units);
  spawn.source = TreasureSpawnSource::DeathDrop;
  spawn.spawned_tick = tick;
  return spawn;
}

std::vector<TreasurePickupCommand> InventoryLootSimulation::drain_pickups_deterministic() {
  std::vector<uint32_t> player_ids{};
  player_ids.reserve(pickup_queue_by_player_.size());
  for (const auto& [player_id, queue] : pickup_queue_by_player_) {
    if (!queue.empty()) {
      player_ids.push_back(player_id);
    }
  }
  std::sort(player_ids.begin(), player_ids.end());

  std::vector<TreasurePickupCommand> drained{};
  for (uint32_t player_id : player_ids) {
    auto queue_it = pickup_queue_by_player_.find(player_id);
    if (queue_it == pickup_queue_by_player_.end()) {
      continue;
    }
    auto& queue = queue_it->second;
    while (!queue.empty()) {
      drained.push_back(queue.front());
      queue.pop_front();
      if (queued_pickup_count_ > 0U) {
        --queued_pickup_count_;
      }
    }
    pickup_queue_by_player_.erase(queue_it);
  }
  return drained;
}

InventorySummary InventoryLootSimulation::to_summary(const PlayerInventoryState& state) const {
  InventorySummary summary{};
  summary.player_id = state.player_id;
  summary.total_value = state.total_value;
  summary.total_weight = state.total_weight;
  summary.item_count = static_cast<uint32_t>(state.items.size());
  summary.last_pickup_seq = state.last_pickup_seq;
  return summary;
}

void InventoryLootSimulation::append_inventory_delta(
    uint32_t player_id, std::unordered_set<uint32_t>& changed_players) const {
  changed_players.insert(player_id);
}

LootDropMode parse_loot_drop_mode(const std::string& value) {
  std::string normalized = lowercase_string(value);
  if (normalized == "none") {
    return LootDropMode::None;
  }
  return LootDropMode::All;
}

const char* to_string(LootDropMode mode) {
  switch (mode) {
    case LootDropMode::None: return "none";
    case LootDropMode::All: return "all";
    default: return "all";
  }
}

const char* to_string(TreasurePickupEnqueueStatus status) {
  switch (status) {
    case TreasurePickupEnqueueStatus::Accepted: return "accepted";
    case TreasurePickupEnqueueStatus::OutOfOrder: return "out_of_order";
    case TreasurePickupEnqueueStatus::QueueFull: return "queue_full";
    default: return "unknown_treasure_pickup_enqueue_status";
  }
}

const char* to_string(TreasurePickupResolveStatus status) {
  switch (status) {
    case TreasurePickupResolveStatus::Collected: return "collected";
    case TreasurePickupResolveStatus::PlayerMissing: return "player_missing";
    case TreasurePickupResolveStatus::PlayerDead: return "player_dead";
    case TreasurePickupResolveStatus::UnknownSpawn: return "unknown_spawn";
    case TreasurePickupResolveStatus::DuplicatePickup: return "duplicate_pickup";
    case TreasurePickupResolveStatus::OutOfRange: return "out_of_range";
    case TreasurePickupResolveStatus::InventoryCapacityExceeded: return "inventory_capacity_exceeded";
    case TreasurePickupResolveStatus::WeightLimitExceeded: return "weight_limit_exceeded";
    default: return "unknown_treasure_pickup_resolve_status";
  }
}

const char* to_string(TreasureSpawnSource source) {
  switch (source) {
    case TreasureSpawnSource::Scheduled: return "scheduled";
    case TreasureSpawnSource::DeathDrop: return "death_drop";
    default: return "unknown";
  }
}

} // namespace devy::server
