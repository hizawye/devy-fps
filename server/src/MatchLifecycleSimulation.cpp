#include "server/MatchLifecycleSimulation.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <limits>
#include <unordered_set>

namespace devy::server {
namespace {

constexpr uint32_t kDefaultMatchDurationSeconds = 600U;
constexpr uint32_t kDefaultRespawnDelayTicks = 60U;
constexpr uint32_t kDefaultMinPlayersToStart = 1U;

constexpr auto kOneSecond = std::chrono::seconds(1);

} // namespace

MatchLifecycleSimulation::MatchLifecycleSimulation(MatchLifecycleConfig config)
    : config_(sanitize_config(config)),
      pre_match_ticks_remaining_(config_.pre_match_ticks),
      in_match_seconds_remaining_(config_.match_duration_seconds) {}

void MatchLifecycleSimulation::reset() {
  phase_ = MatchPhase::PreMatch;
  pre_match_ticks_remaining_ = config_.pre_match_ticks;
  in_match_seconds_remaining_ = config_.match_duration_seconds;
  in_match_second_accumulator_ = std::chrono::nanoseconds::zero();
  phase_started_tick_ = 0U;
  winner_player_id_.reset();

  for (auto& [player_id, record] : players_by_id_) {
    static_cast<void>(player_id);
    record.kills = 0U;
    record.deaths = 0U;
    record.coins = 0;
    record.respawns_remaining = config_.respawns_per_player;
    record.alive = true;
    record.eliminated = false;
    record.pending_respawn_tick.reset();
  }
}

void MatchLifecycleSimulation::ensure_player(uint32_t player_id) {
  auto [it, inserted] = players_by_id_.try_emplace(player_id);
  if (inserted) {
    it->second.player_id = player_id;
    it->second.respawns_remaining = config_.respawns_per_player;
  }
  it->second.participated = true;
  it->second.connected = true;
}

void MatchLifecycleSimulation::remove_player(uint32_t player_id) {
  auto it = players_by_id_.find(player_id);
  if (it == players_by_id_.end()) {
    return;
  }
  it->second.connected = false;
  it->second.pending_respawn_tick.reset();
}

MatchLifecycleTickResult MatchLifecycleSimulation::resolve_tick(
    uint64_t tick, std::chrono::nanoseconds tick_interval,
    const std::vector<uint32_t>& active_player_ids,
    const std::vector<PlayerCombatState>& combat_states,
    const std::vector<DeathResolution>& death_events,
    const std::vector<InventorySummary>& inventory_summaries) {
  MatchLifecycleTickResult out{};
  bool timer_expired = false;

  std::unordered_set<uint32_t> active_set{};
  active_set.reserve(active_player_ids.size());
  for (uint32_t player_id : active_player_ids) {
    active_set.insert(player_id);
    ensure_player(player_id);
  }
  for (auto& [player_id, record] : players_by_id_) {
    record.connected = active_set.find(player_id) != active_set.end();
  }

  for (const auto& combat_state : combat_states) {
    ensure_player(combat_state.player_id);
    auto it = players_by_id_.find(combat_state.player_id);
    if (it == players_by_id_.end()) {
      continue;
    }
    if (!it->second.pending_respawn_tick.has_value()) {
      it->second.alive = combat_state.alive;
    }
  }

  for (const auto& summary : inventory_summaries) {
    ensure_player(summary.player_id);
    auto it = players_by_id_.find(summary.player_id);
    if (it == players_by_id_.end()) {
      continue;
    }
    if (it->second.coins != summary.total_value) {
      it->second.coins = summary.total_value;
      out.scoreboard_changed = true;
    }
  }

  bool started_this_tick = false;
  if (phase_ == MatchPhase::PreMatch) {
    if (connected_count() < config_.min_players_to_start) {
      if (pre_match_ticks_remaining_ != config_.pre_match_ticks) {
        pre_match_ticks_remaining_ = config_.pre_match_ticks;
        out.state_changed = true;
      }
    } else {
      if (pre_match_ticks_remaining_ > 0U) {
        --pre_match_ticks_remaining_;
      }
      if (pre_match_ticks_remaining_ == 0U) {
        enter_in_match(tick);
        out.state_changed = true;
        started_this_tick = true;
      }
    }
  }

  if (phase_ == MatchPhase::InMatch && !started_this_tick) {
    if (tick_interval > std::chrono::nanoseconds::zero() && in_match_seconds_remaining_ > 0U) {
      in_match_second_accumulator_ += tick_interval;
      while (in_match_second_accumulator_ >= kOneSecond && in_match_seconds_remaining_ > 0U) {
        in_match_second_accumulator_ -= kOneSecond;
        --in_match_seconds_remaining_;
      }
      timer_expired = (in_match_seconds_remaining_ == 0U);
    }
  }

  if (phase_ == MatchPhase::InMatch && !death_events.empty()) {
    std::vector<DeathResolution> sorted_deaths = death_events;
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

    std::unordered_set<uint32_t> handled_victims{};
    handled_victims.reserve(sorted_deaths.size());
    for (const auto& death : sorted_deaths) {
      if (!handled_victims.insert(death.victim_id).second) {
        continue;
      }

      ensure_player(death.victim_id);
      auto victim_it = players_by_id_.find(death.victim_id);
      if (victim_it == players_by_id_.end()) {
        continue;
      }
      auto& victim = victim_it->second;
      ++victim.deaths;
      victim.alive = false;
      victim.pending_respawn_tick.reset();
      if (victim.respawns_remaining > 0U) {
        --victim.respawns_remaining;
        victim.eliminated = false;
        victim.pending_respawn_tick = tick + config_.respawn_delay_ticks;
      } else {
        victim.eliminated = true;
      }
      out.scoreboard_changed = true;

      if (death.killer_id != 0U && death.killer_id != death.victim_id) {
        ensure_player(death.killer_id);
        auto killer_it = players_by_id_.find(death.killer_id);
        if (killer_it != players_by_id_.end()) {
          ++killer_it->second.kills;
          out.scoreboard_changed = true;
        }
      }
    }
  }

  if (phase_ == MatchPhase::InMatch) {
    std::vector<uint32_t> sorted_player_ids{};
    sorted_player_ids.reserve(players_by_id_.size());
    for (const auto& [player_id, record] : players_by_id_) {
      static_cast<void>(record);
      sorted_player_ids.push_back(player_id);
    }
    std::sort(sorted_player_ids.begin(), sorted_player_ids.end());

    for (uint32_t player_id : sorted_player_ids) {
      auto it = players_by_id_.find(player_id);
      if (it == players_by_id_.end()) {
        continue;
      }
      auto& player = it->second;
      if (!player.pending_respawn_tick.has_value()) {
        continue;
      }
      if (tick < player.pending_respawn_tick.value()) {
        continue;
      }
      if (player.eliminated) {
        player.pending_respawn_tick.reset();
        continue;
      }
      player.pending_respawn_tick.reset();
      player.alive = true;
      out.respawned_players.push_back(player_id);
      out.scoreboard_changed = true;
    }

    std::vector<uint32_t> contenders{};
    for (const auto& [player_id, player] : players_by_id_) {
      if (!player.connected || player.eliminated) {
        continue;
      }
      contenders.push_back(player_id);
    }
    std::sort(contenders.begin(), contenders.end());
    if (contenders.size() == 1U) {
      enter_post_match(tick, contenders.front());
      out.state_changed = true;
    }
  }

  if (phase_ == MatchPhase::InMatch && timer_expired) {
    const auto scoreboard = build_scoreboard();
    enter_post_match(tick, resolve_winner_from_scoreboard(scoreboard));
    out.state_changed = true;
  }

  out.state = build_state(tick_interval);
  out.scoreboard = build_scoreboard();
  return out;
}

MatchLifecycleState MatchLifecycleSimulation::state(std::chrono::nanoseconds tick_interval) const {
  return build_state(tick_interval);
}

std::vector<MatchScoreEntry> MatchLifecycleSimulation::scoreboard_snapshot() const {
  return build_scoreboard();
}

std::optional<MatchScoreEntry> MatchLifecycleSimulation::score_for(uint32_t player_id) const {
  auto it = players_by_id_.find(player_id);
  if (it == players_by_id_.end() || !it->second.participated) {
    return std::nullopt;
  }

  MatchScoreEntry score{};
  score.player_id = it->second.player_id;
  score.kills = it->second.kills;
  score.deaths = it->second.deaths;
  score.coins = it->second.coins;
  score.respawns_remaining = it->second.respawns_remaining;
  score.alive = it->second.alive;
  score.eliminated = it->second.eliminated;
  score.connected = it->second.connected;
  return score;
}

MatchLifecycleConfig MatchLifecycleSimulation::sanitize_config(MatchLifecycleConfig config) {
  if (config.match_duration_seconds == 0U) {
    config.match_duration_seconds = kDefaultMatchDurationSeconds;
  }
  if (config.respawn_delay_ticks == 0U) {
    config.respawn_delay_ticks = kDefaultRespawnDelayTicks;
  }
  if (config.min_players_to_start == 0U) {
    config.min_players_to_start = kDefaultMinPlayersToStart;
  }
  return config;
}

uint32_t MatchLifecycleSimulation::ticks_to_remaining_seconds(
    uint32_t ticks, std::chrono::nanoseconds tick_interval) {
  if (ticks == 0U) {
    return 0U;
  }
  if (tick_interval <= std::chrono::nanoseconds::zero()) {
    return ticks;
  }

  const long double tick_ns = static_cast<long double>(tick_interval.count());
  const long double total_seconds =
      (tick_ns * static_cast<long double>(ticks)) / 1'000'000'000.0L;
  const long double ceil_seconds = std::ceil(total_seconds);
  if (!std::isfinite(static_cast<double>(ceil_seconds)) || ceil_seconds <= 0.0L) {
    return 0U;
  }
  if (ceil_seconds >= static_cast<long double>(std::numeric_limits<uint32_t>::max())) {
    return std::numeric_limits<uint32_t>::max();
  }
  return static_cast<uint32_t>(ceil_seconds);
}

bool MatchLifecycleSimulation::ranking_better(const MatchScoreEntry& lhs,
                                              const MatchScoreEntry& rhs) {
  if (lhs.kills != rhs.kills) {
    return lhs.kills > rhs.kills;
  }
  if (lhs.coins != rhs.coins) {
    return lhs.coins > rhs.coins;
  }
  if (lhs.deaths != rhs.deaths) {
    return lhs.deaths < rhs.deaths;
  }
  return lhs.player_id < rhs.player_id;
}

MatchLifecycleState MatchLifecycleSimulation::build_state(
    std::chrono::nanoseconds tick_interval) const {
  MatchLifecycleState state{};
  state.phase = phase_;
  state.phase_started_tick = phase_started_tick_;
  state.winner_player_id = winner_player_id_;
  switch (phase_) {
    case MatchPhase::PreMatch:
      state.remaining_seconds =
          ticks_to_remaining_seconds(pre_match_ticks_remaining_, tick_interval);
      break;
    case MatchPhase::InMatch:
      state.remaining_seconds = in_match_seconds_remaining_;
      break;
    case MatchPhase::PostMatch:
      state.remaining_seconds = 0U;
      break;
    default:
      state.remaining_seconds = 0U;
      break;
  }
  return state;
}

std::optional<uint32_t> MatchLifecycleSimulation::resolve_winner_from_scoreboard(
    const std::vector<MatchScoreEntry>& scoreboard) const {
  if (scoreboard.empty()) {
    return std::nullopt;
  }
  return scoreboard.front().player_id;
}

std::vector<MatchScoreEntry> MatchLifecycleSimulation::build_scoreboard() const {
  std::vector<MatchScoreEntry> scoreboard{};
  scoreboard.reserve(players_by_id_.size());
  for (const auto& [player_id, player] : players_by_id_) {
    static_cast<void>(player_id);
    if (!player.participated) {
      continue;
    }
    scoreboard.push_back({player.player_id,
                          player.kills,
                          player.deaths,
                          player.coins,
                          player.respawns_remaining,
                          player.alive,
                          player.eliminated,
                          player.connected});
  }
  std::sort(scoreboard.begin(), scoreboard.end(), ranking_better);
  return scoreboard;
}

std::size_t MatchLifecycleSimulation::connected_count() const {
  std::size_t count = 0U;
  for (const auto& [player_id, player] : players_by_id_) {
    static_cast<void>(player_id);
    if (player.connected) {
      ++count;
    }
  }
  return count;
}

void MatchLifecycleSimulation::enter_in_match(uint64_t tick) {
  phase_ = MatchPhase::InMatch;
  in_match_seconds_remaining_ = config_.match_duration_seconds;
  in_match_second_accumulator_ = std::chrono::nanoseconds::zero();
  winner_player_id_.reset();
  phase_started_tick_ = tick;

  for (auto& [player_id, player] : players_by_id_) {
    static_cast<void>(player_id);
    player.alive = true;
    player.eliminated = false;
    player.pending_respawn_tick.reset();
    player.respawns_remaining = config_.respawns_per_player;
  }
}

void MatchLifecycleSimulation::enter_post_match(
    uint64_t tick, const std::optional<uint32_t>& winner_player_id) {
  phase_ = MatchPhase::PostMatch;
  in_match_seconds_remaining_ = 0U;
  pre_match_ticks_remaining_ = 0U;
  in_match_second_accumulator_ = std::chrono::nanoseconds::zero();
  winner_player_id_ = winner_player_id;
  phase_started_tick_ = tick;
}

const char* to_string(MatchPhase phase) {
  switch (phase) {
    case MatchPhase::PreMatch:
      return "pre_match";
    case MatchPhase::InMatch:
      return "in_match";
    case MatchPhase::PostMatch:
      return "post_match";
    default:
      return "unknown_match_phase";
  }
}

} // namespace devy::server
