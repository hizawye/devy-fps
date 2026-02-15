#pragma once

#include "server/CombatSimulation.h"
#include "server/InventoryLootSimulation.h"

#include <chrono>
#include <cstdint>
#include <optional>
#include <unordered_map>
#include <vector>

namespace devy::server {

enum class MatchPhase : uint8_t {
  PreMatch = 0,
  InMatch,
  PostMatch
};

struct MatchLifecycleConfig {
  uint32_t pre_match_ticks{90};
  uint32_t match_duration_seconds{600};
  uint32_t respawns_per_player{2};
  uint32_t respawn_delay_ticks{60};
  uint32_t min_players_to_start{1};
};

struct MatchScoreEntry {
  uint32_t player_id{0};
  uint32_t kills{0};
  uint32_t deaths{0};
  int coins{0};
  uint32_t respawns_remaining{0};
  bool alive{true};
  bool eliminated{false};
  bool connected{false};
};

struct MatchLifecycleState {
  MatchPhase phase{MatchPhase::PreMatch};
  uint32_t remaining_seconds{0};
  uint64_t phase_started_tick{0};
  std::optional<uint32_t> winner_player_id{};
};

struct MatchLifecycleTickResult {
  MatchLifecycleState state{};
  std::vector<MatchScoreEntry> scoreboard{};
  std::vector<uint32_t> respawned_players{};
  bool state_changed{false};
  bool scoreboard_changed{false};
};

class MatchLifecycleSimulation {
public:
  explicit MatchLifecycleSimulation(MatchLifecycleConfig config = {});

  void reset();
  void ensure_player(uint32_t player_id);
  void remove_player(uint32_t player_id);

  MatchLifecycleTickResult resolve_tick(
      uint64_t tick, std::chrono::nanoseconds tick_interval,
      const std::vector<uint32_t>& active_player_ids,
      const std::vector<PlayerCombatState>& combat_states,
      const std::vector<DeathResolution>& death_events,
      const std::vector<InventorySummary>& inventory_summaries);

  [[nodiscard]] MatchLifecycleState state(std::chrono::nanoseconds tick_interval) const;
  [[nodiscard]] std::vector<MatchScoreEntry> scoreboard_snapshot() const;
  [[nodiscard]] std::optional<MatchScoreEntry> score_for(uint32_t player_id) const;

private:
  struct PlayerRecord {
    uint32_t player_id{0};
    uint32_t kills{0};
    uint32_t deaths{0};
    int coins{0};
    uint32_t respawns_remaining{0};
    bool alive{true};
    bool eliminated{false};
    bool connected{false};
    bool participated{false};
    std::optional<uint64_t> pending_respawn_tick{};
  };

  [[nodiscard]] static MatchLifecycleConfig sanitize_config(MatchLifecycleConfig config);
  [[nodiscard]] static uint32_t ticks_to_remaining_seconds(
      uint32_t ticks, std::chrono::nanoseconds tick_interval);
  [[nodiscard]] static bool ranking_better(const MatchScoreEntry& lhs, const MatchScoreEntry& rhs);

  [[nodiscard]] MatchLifecycleState build_state(std::chrono::nanoseconds tick_interval) const;
  [[nodiscard]] std::optional<uint32_t> resolve_winner_from_scoreboard(
      const std::vector<MatchScoreEntry>& scoreboard) const;
  [[nodiscard]] std::vector<MatchScoreEntry> build_scoreboard() const;
  [[nodiscard]] std::size_t connected_count() const;

  void enter_in_match(uint64_t tick);
  void enter_post_match(uint64_t tick, const std::optional<uint32_t>& winner_player_id);

  MatchLifecycleConfig config_{};
  MatchPhase phase_{MatchPhase::PreMatch};
  uint32_t pre_match_ticks_remaining_{0};
  uint32_t in_match_seconds_remaining_{0};
  std::chrono::nanoseconds in_match_second_accumulator_{};
  uint64_t phase_started_tick_{0};
  std::optional<uint32_t> winner_player_id_{};
  std::unordered_map<uint32_t, PlayerRecord> players_by_id_{};
};

const char* to_string(MatchPhase phase);

} // namespace devy::server
