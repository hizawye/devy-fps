#include "server/MatchLifecycleSimulation.h"

#include <catch2/catch_test_macros.hpp>

#include <chrono>
#include <cstdint>
#include <vector>

namespace devy::server {
namespace {

std::vector<PlayerCombatState> combat_states(std::initializer_list<PlayerCombatState> states) {
  return std::vector<PlayerCombatState>(states);
}

std::vector<InventorySummary> inventory_summaries(std::initializer_list<InventorySummary> summaries) {
  return std::vector<InventorySummary>(summaries);
}

TEST_CASE("Match lifecycle transitions pre-match to in-match to post-match on authoritative timer") {
  MatchLifecycleSimulation simulation({3U, 4U, 1U, 2U, 1U});
  const auto dt = std::chrono::seconds(1);

  const auto tick_1 = simulation.resolve_tick(
      1U, dt, {1U, 2U}, combat_states({{1U, 100, true, 0U}, {2U, 100, true, 0U}}), {},
      inventory_summaries({{1U, 0, 0, 0U, 0U}, {2U, 0, 0, 0U, 0U}}));
  REQUIRE(tick_1.state.phase == MatchPhase::PreMatch);
  REQUIRE(tick_1.state.remaining_seconds == 2U);

  const auto tick_2 = simulation.resolve_tick(
      2U, dt, {1U, 2U}, combat_states({{1U, 100, true, 0U}, {2U, 100, true, 0U}}), {},
      inventory_summaries({{1U, 0, 0, 0U, 0U}, {2U, 0, 0, 0U, 0U}}));
  REQUIRE(tick_2.state.phase == MatchPhase::PreMatch);
  REQUIRE(tick_2.state.remaining_seconds == 1U);

  const auto tick_3 = simulation.resolve_tick(
      3U, dt, {1U, 2U}, combat_states({{1U, 100, true, 0U}, {2U, 100, true, 0U}}), {},
      inventory_summaries({{1U, 0, 0, 0U, 0U}, {2U, 0, 0, 0U, 0U}}));
  REQUIRE(tick_3.state.phase == MatchPhase::InMatch);
  REQUIRE(tick_3.state_changed);
  REQUIRE(tick_3.state.remaining_seconds == 4U);

  const auto tick_4 = simulation.resolve_tick(
      4U, dt, {1U, 2U}, combat_states({{1U, 100, true, 0U}, {2U, 100, true, 0U}}), {},
      inventory_summaries({{1U, 0, 0, 0U, 0U}, {2U, 0, 0, 0U, 0U}}));
  REQUIRE(tick_4.state.phase == MatchPhase::InMatch);
  REQUIRE(tick_4.state.remaining_seconds == 3U);

  const auto tick_5 = simulation.resolve_tick(
      5U, dt, {1U, 2U}, combat_states({{1U, 100, true, 0U}, {2U, 100, true, 0U}}), {},
      inventory_summaries({{1U, 0, 0, 0U, 0U}, {2U, 0, 0, 0U, 0U}}));
  REQUIRE(tick_5.state.phase == MatchPhase::InMatch);
  REQUIRE(tick_5.state.remaining_seconds == 2U);

  const auto tick_6 = simulation.resolve_tick(
      6U, dt, {1U, 2U}, combat_states({{1U, 100, true, 0U}, {2U, 100, true, 0U}}), {},
      inventory_summaries({{1U, 0, 0, 0U, 0U}, {2U, 0, 0, 0U, 0U}}));
  REQUIRE(tick_6.state.phase == MatchPhase::InMatch);
  REQUIRE(tick_6.state.remaining_seconds == 1U);

  const auto tick_7 = simulation.resolve_tick(
      7U, dt, {1U, 2U}, combat_states({{1U, 100, true, 0U}, {2U, 100, true, 0U}}), {},
      inventory_summaries({{1U, 0, 0, 0U, 0U}, {2U, 0, 0, 0U, 0U}}));
  REQUIRE(tick_7.state.phase == MatchPhase::PostMatch);
  REQUIRE(tick_7.state_changed);
  REQUIRE(tick_7.state.remaining_seconds == 0U);
  REQUIRE(tick_7.state.winner_player_id.has_value());
  REQUIRE(tick_7.state.winner_player_id.value() == 1U);
}

TEST_CASE("Match lifecycle enforces respawn budget and ends when only one contender remains") {
  MatchLifecycleSimulation simulation({1U, 60U, 1U, 2U, 2U});
  const auto dt = std::chrono::seconds(1);

  const auto tick_1 = simulation.resolve_tick(
      1U, dt, {1U, 2U}, combat_states({{1U, 100, true, 0U}, {2U, 100, true, 0U}}), {},
      inventory_summaries({{1U, 0, 0, 0U, 0U}, {2U, 0, 0, 0U, 0U}}));
  REQUIRE(tick_1.state.phase == MatchPhase::InMatch);

  const auto tick_2 = simulation.resolve_tick(
      2U, dt, {1U, 2U}, combat_states({{1U, 100, true, 0U}, {2U, 0, false, 0U}}),
      {{2U, 2U, 1U, 7U, "railgun"}},
      inventory_summaries({{1U, 0, 0, 0U, 0U}, {2U, 0, 0, 0U, 0U}}));
  REQUIRE(tick_2.respawned_players.empty());
  REQUIRE(tick_2.state.phase == MatchPhase::InMatch);
  REQUIRE(simulation.score_for(2U).has_value());
  REQUIRE(simulation.score_for(2U)->respawns_remaining == 0U);
  REQUIRE_FALSE(simulation.score_for(2U)->eliminated);

  const auto tick_3 = simulation.resolve_tick(
      3U, dt, {1U, 2U}, combat_states({{1U, 100, true, 0U}, {2U, 0, false, 0U}}), {},
      inventory_summaries({{1U, 0, 0, 0U, 0U}, {2U, 0, 0, 0U, 0U}}));
  REQUIRE(tick_3.respawned_players.empty());

  const auto tick_4 = simulation.resolve_tick(
      4U, dt, {1U, 2U}, combat_states({{1U, 100, true, 0U}, {2U, 0, false, 0U}}), {},
      inventory_summaries({{1U, 0, 0, 0U, 0U}, {2U, 0, 0, 0U, 0U}}));
  REQUIRE(tick_4.respawned_players.size() == 1U);
  REQUIRE(tick_4.respawned_players.front() == 2U);

  const auto tick_5 = simulation.resolve_tick(
      5U, dt, {1U, 2U}, combat_states({{1U, 100, true, 0U}, {2U, 0, false, 0U}}),
      {{5U, 2U, 1U, 8U, "railgun"}},
      inventory_summaries({{1U, 0, 0, 0U, 0U}, {2U, 0, 0, 0U, 0U}}));
  REQUIRE(tick_5.state.phase == MatchPhase::PostMatch);
  REQUIRE(tick_5.state.winner_player_id.has_value());
  REQUIRE(tick_5.state.winner_player_id.value() == 1U);
  REQUIRE(simulation.score_for(2U).has_value());
  REQUIRE(simulation.score_for(2U)->eliminated);
}

TEST_CASE("Match lifecycle timer handles multi-second tick intervals without underflow") {
  MatchLifecycleSimulation simulation({1U, 2U, 0U, 1U, 2U});

  const auto tick_1 = simulation.resolve_tick(
      1U, std::chrono::seconds(1), {1U, 2U},
      combat_states({{1U, 100, true, 0U}, {2U, 100, true, 0U}}), {},
      inventory_summaries({{1U, 0, 0, 0U, 0U}, {2U, 0, 0, 0U, 0U}}));
  REQUIRE(tick_1.state.phase == MatchPhase::InMatch);
  REQUIRE(tick_1.state.remaining_seconds == 2U);

  const auto tick_2 = simulation.resolve_tick(
      2U, std::chrono::milliseconds(2500), {1U, 2U},
      combat_states({{1U, 100, true, 0U}, {2U, 100, true, 0U}}), {},
      inventory_summaries({{1U, 0, 0, 0U, 0U}, {2U, 0, 0, 0U, 0U}}));
  REQUIRE(tick_2.state.phase == MatchPhase::PostMatch);
  REQUIRE(tick_2.state.remaining_seconds == 0U);
  REQUIRE(tick_2.state.winner_player_id.has_value());
  REQUIRE(tick_2.state.winner_player_id.value() == 1U);
}

TEST_CASE("Match lifecycle winner resolution is deterministic by kills then coins then deaths") {
  MatchLifecycleSimulation simulation({1U, 1U, 0U, 1U, 3U});
  const auto dt = std::chrono::seconds(1);

  const auto tick_1 = simulation.resolve_tick(
      1U, dt, {1U, 2U, 3U},
      combat_states({{1U, 100, true, 0U}, {2U, 100, true, 0U}, {3U, 100, true, 0U}}), {},
      inventory_summaries({{1U, 0, 0, 0U, 0U}, {2U, 0, 0, 0U, 0U}, {3U, 0, 0, 0U, 0U}}));
  REQUIRE(tick_1.state.phase == MatchPhase::InMatch);

  const auto tick_2 = simulation.resolve_tick(
      2U, dt, {1U, 2U, 3U},
      combat_states({{1U, 0, false, 0U}, {2U, 100, true, 0U}, {3U, 0, false, 0U}}),
      {{2U, 3U, 1U, 10U, "rifle"}, {2U, 1U, 2U, 11U, "rifle"}},
      inventory_summaries({{1U, 5, 0, 0U, 0U}, {2U, 10, 0, 0U, 0U}, {3U, 1, 0, 0U, 0U}}));
  REQUIRE(tick_2.state.phase == MatchPhase::PostMatch);
  REQUIRE(tick_2.state.winner_player_id.has_value());
  REQUIRE(tick_2.state.winner_player_id.value() == 2U);
}

} // namespace
} // namespace devy::server
