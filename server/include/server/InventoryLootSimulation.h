#pragma once

#include "server/AuthoritativeLoop.h"
#include "server/CombatSimulation.h"
#include "server/MovementSimulation.h"
#include "shared/game/Treasure.h"

#include <cstdint>
#include <deque>
#include <optional>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace devy::server {

enum class LootDropMode : uint8_t {
  None = 0,
  All
};

struct InventoryLootConfig {
  uint32_t spawn_interval_ticks{90};
  std::size_t max_active_spawns{64};
  std::size_t pickup_queue_capacity{2048};
  float pickup_radius_units{2.5F};
  std::size_t max_items_per_player{16};
  int max_weight_per_player{40};
  LootDropMode loot_drop_mode{LootDropMode::All};
  int world_size_x_units{2048};
  int world_size_y_units{2048};
  float death_drop_spread_units{0.75F};
};

struct TreasurePickupCommand {
  uint32_t player_id{0};
  uint32_t pickup_seq{0};
  uint64_t spawn_id{0};
  RuntimeTimePoint received_at{};
};

enum class TreasurePickupEnqueueStatus : uint8_t {
  Accepted = 0,
  OutOfOrder,
  QueueFull
};

enum class TreasurePickupResolveStatus : uint8_t {
  Collected = 0,
  PlayerMissing,
  PlayerDead,
  UnknownSpawn,
  DuplicatePickup,
  OutOfRange,
  InventoryCapacityExceeded,
  WeightLimitExceeded
};

enum class TreasureSpawnSource : uint8_t {
  Scheduled = 0,
  DeathDrop
};

struct TreasureSpawnInstance {
  uint64_t spawn_id{0};
  std::string treasure_id{};
  int value{0};
  int weight{0};
  float position_x{0.0F};
  float position_y{0.0F};
  TreasureSpawnSource source{TreasureSpawnSource::Scheduled};
  uint64_t spawned_tick{0};
};

struct InventorySummary {
  uint32_t player_id{0};
  int total_value{0};
  int total_weight{0};
  uint32_t item_count{0};
  uint32_t last_pickup_seq{0};
};

struct TreasurePickupResolution {
  uint32_t player_id{0};
  uint32_t pickup_seq{0};
  uint64_t spawn_id{0};
  TreasurePickupResolveStatus status{TreasurePickupResolveStatus::UnknownSpawn};
  std::string treasure_id{};
  int total_value{0};
  int total_weight{0};
  uint32_t item_count{0};
};

struct InventoryLootTickResult {
  std::vector<TreasureSpawnInstance> spawned{};
  std::vector<TreasurePickupResolution> pickup_results{};
  std::vector<InventorySummary> inventory_deltas{};
};

class InventoryLootSimulation {
public:
  explicit InventoryLootSimulation(std::vector<devy::game::TreasureDefinition> treasure_definitions,
                                   InventoryLootConfig config = {});

  void reset();
  void ensure_player(uint32_t player_id);
  void remove_player(uint32_t player_id);

  TreasurePickupEnqueueStatus enqueue_pickup(const TreasurePickupCommand& command);
  InventoryLootTickResult resolve_tick(uint64_t tick,
                                       const std::vector<PlayerMotionState>& movement_states,
                                       const std::vector<PlayerCombatState>& combat_states,
                                       const std::vector<DeathResolution>& deaths);

  [[nodiscard]] std::optional<InventorySummary> summary_for(uint32_t player_id) const;
  [[nodiscard]] std::vector<InventorySummary> inventory_snapshot() const;
  [[nodiscard]] std::vector<TreasureSpawnInstance> active_spawns() const;

private:
  struct InventoryItem {
    uint64_t source_spawn_id{0};
    devy::game::TreasureDefinition treasure{};
  };

  struct PlayerInventoryState {
    uint32_t player_id{0};
    float position_x{0.0F};
    float position_y{0.0F};
    bool alive{true};
    int total_value{0};
    int total_weight{0};
    uint32_t last_pickup_seq{0};
    uint32_t last_enqueued_pickup_seq{0};
    std::vector<InventoryItem> items{};
  };

  [[nodiscard]] static InventoryLootConfig sanitize_config(InventoryLootConfig config);
  [[nodiscard]] static bool is_sorted_player_id(const std::vector<DeathResolution>& deaths);

  [[nodiscard]] TreasureSpawnInstance create_scheduled_spawn(uint64_t tick);
  [[nodiscard]] TreasureSpawnInstance create_death_drop_spawn(
      uint64_t tick, const devy::game::TreasureDefinition& treasure, float origin_x, float origin_y,
      std::size_t item_index);
  [[nodiscard]] std::vector<TreasurePickupCommand> drain_pickups_deterministic();
  [[nodiscard]] InventorySummary to_summary(const PlayerInventoryState& state) const;

  void append_inventory_delta(uint32_t player_id, std::unordered_set<uint32_t>& changed_players) const;

  std::vector<devy::game::TreasureDefinition> treasure_definitions_{};
  InventoryLootConfig config_{};
  std::unordered_map<uint32_t, PlayerInventoryState> players_by_id_{};
  std::unordered_map<uint32_t, std::deque<TreasurePickupCommand>> pickup_queue_by_player_{};
  std::unordered_map<uint64_t, TreasureSpawnInstance> active_spawns_by_id_{};
  std::unordered_set<uint64_t> consumed_spawn_ids_{};
  uint64_t next_spawn_id_{1U};
  std::size_t next_spawn_definition_index_{0U};
  std::size_t queued_pickup_count_{0U};
};

LootDropMode parse_loot_drop_mode(const std::string& value);
const char* to_string(LootDropMode mode);
const char* to_string(TreasurePickupEnqueueStatus status);
const char* to_string(TreasurePickupResolveStatus status);
const char* to_string(TreasureSpawnSource source);

} // namespace devy::server
