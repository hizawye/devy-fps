#include "server/AuthoritativeLoop.h"
#include "server/CombatEvents.h"
#include "server/CombatSimulation.h"
#include "server/MovementSimulation.h"

#include <catch2/catch_test_macros.hpp>

#include <chrono>
#include <cstdint>
#include <optional>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

namespace devy::server {
namespace {

std::vector<devy::game::WeaponDefinition> integration_weapon_fixture() {
  return {{"railgun", "hitscan", 120, 4, 1.0F, 0, 0, 128.0F, 0.0F, 3},
          {"launcher", "projectile", 120, 2, 1.0F, 0, 0, 128.0F, 20.0F, 3}};
}

nlohmann::json build_snapshot_payload(
    uint64_t tick, const std::vector<std::pair<uint32_t, std::string>>& players,
    const std::vector<PlayerMotionState>& movement_states,
    const std::vector<PlayerCombatState>& combat_states, const nlohmann::json& events) {
  std::unordered_map<uint32_t, PlayerMotionState> movement_by_player{};
  movement_by_player.reserve(movement_states.size());
  for (const auto& movement_state : movement_states) {
    movement_by_player[movement_state.player_id] = movement_state;
  }

  std::unordered_map<uint32_t, PlayerCombatState> combat_by_player{};
  combat_by_player.reserve(combat_states.size());
  for (const auto& combat_state : combat_states) {
    combat_by_player[combat_state.player_id] = combat_state;
  }

  nlohmann::json snapshot_players = nlohmann::json::array();
  for (const auto& [player_id, player_name] : players) {
    float position_x = 0.0F;
    float position_y = 0.0F;
    float velocity_x = 0.0F;
    float velocity_y = 0.0F;
    uint32_t last_processed_input_seq = 0U;

    const auto movement_it = movement_by_player.find(player_id);
    if (movement_it != movement_by_player.end()) {
      position_x = movement_it->second.position_x;
      position_y = movement_it->second.position_y;
      velocity_x = movement_it->second.velocity_x;
      velocity_y = movement_it->second.velocity_y;
      last_processed_input_seq = movement_it->second.last_processed_input_seq;
    }

    int health = 100;
    bool alive = true;
    uint32_t last_shot_seq = 0U;
    const auto combat_it = combat_by_player.find(player_id);
    if (combat_it != combat_by_player.end()) {
      health = combat_it->second.health;
      alive = combat_it->second.alive;
      last_shot_seq = combat_it->second.last_shot_seq;
    }

    snapshot_players.push_back({{"player_id", player_id},
                                {"player_name", player_name},
                                {"position", {{"x", position_x}, {"y", position_y}}},
                                {"velocity", {{"x", velocity_x}, {"y", velocity_y}}},
                                {"last_processed_input_seq", last_processed_input_seq},
                                {"health", health},
                                {"alive", alive},
                                {"last_shot_seq", last_shot_seq}});
  }

  return {{"tick", tick}, {"players", snapshot_players}, {"events", events}};
}

const nlohmann::json* find_player(const nlohmann::json& snapshot, uint32_t player_id) {
  if (!snapshot.is_object() || !snapshot.contains("players") || !snapshot["players"].is_array()) {
    return nullptr;
  }
  for (const auto& player : snapshot["players"]) {
    if (!player.is_object() || !player.contains("player_id")) {
      continue;
    }
    if (player["player_id"].get<uint32_t>() == player_id) {
      return &player;
    }
  }
  return nullptr;
}

TEST_CASE("Combat integration emits lethal damage broadcasts and snapshot reconciliation fields") {
  const RuntimeTimePoint t0{};
  AuthoritativeLoop loop({10U, 32U, 1U}, t0);
  MovementSimulation movement({6.0F});
  CombatSimulation combat(integration_weapon_fixture());

  const std::vector<std::pair<uint32_t, std::string>> players{
      {1U, "alpha"},
      {2U, "bravo"},
  };
  for (const auto& [player_id, player_name] : players) {
    static_cast<void>(player_name);
    movement.ensure_player(player_id);
    combat.ensure_player(player_id);
  }

  REQUIRE(loop.enqueue_input({1U, 9U, 0.0F, 0.0F, false, false, t0}) ==
          InputEnqueueStatus::Accepted);
  REQUIRE(loop.enqueue_input({2U, 4U, 0.0F, 0.0F, false, false, t0}) ==
          InputEnqueueStatus::Accepted);
  REQUIRE(combat.enqueue_fire({1U, 1U, "railgun", -5.0F, 0.0F, 1.0F, 0.0F, t0}) ==
          FireEnqueueStatus::Accepted);

  const auto frames = loop.advance(t0 + std::chrono::milliseconds(100));
  REQUIRE(frames.size() == 1U);
  REQUIRE(frames.front().snapshot_due);

  nlohmann::json pending_events = nlohmann::json::array();
  std::vector<devy::net::Packet> broadcasts{};
  nlohmann::json snapshot_payload{};

  for (const auto& frame : frames) {
    movement.apply_inputs(loop.tick_interval(), frame.inputs);
    const auto movement_states = movement.snapshot();
    combat.set_player_positions(movement_states);
    const auto combat_result = combat.resolve_tick(frame.tick, loop.tick_interval());
    append_combat_outputs(combat_result, pending_events, &broadcasts);
    snapshot_payload =
        build_snapshot_payload(frame.tick, players, movement_states, combat.snapshot(), pending_events);
  }

  REQUIRE(broadcasts.size() == 1U);
  REQUIRE(broadcasts.front().type == devy::net::MessageType::DamageEvent);
  REQUIRE(broadcasts.front().payload["attacker_id"] == 1U);
  REQUIRE(broadcasts.front().payload["victim_id"] == 2U);
  REQUIRE(broadcasts.front().payload["lethal"] == true);
  REQUIRE(broadcasts.front().payload["shot_seq"] == 1U);

  REQUIRE(pending_events.is_array());
  REQUIRE(pending_events.size() == 2U);
  REQUIRE(pending_events[0]["type"] == "damage_event");
  REQUIRE(pending_events[1]["type"] == "death_event");

  const auto* attacker = find_player(snapshot_payload, 1U);
  REQUIRE(attacker != nullptr);
  REQUIRE((*attacker)["last_processed_input_seq"] == 9U);
  REQUIRE((*attacker)["last_shot_seq"] == 1U);
  REQUIRE((*attacker)["health"] == 100);
  REQUIRE((*attacker)["alive"] == true);

  const auto* victim = find_player(snapshot_payload, 2U);
  REQUIRE(victim != nullptr);
  REQUIRE((*victim)["last_processed_input_seq"] == 4U);
  REQUIRE((*victim)["health"] == 0);
  REQUIRE((*victim)["alive"] == false);
}

TEST_CASE("Combat integration keeps projectile damage deferred until travel delay tick") {
  const RuntimeTimePoint t0{};
  AuthoritativeLoop loop({10U, 32U, 1U}, t0);
  MovementSimulation movement({6.0F});
  CombatSimulation combat(integration_weapon_fixture());

  const std::vector<std::pair<uint32_t, std::string>> players{
      {1U, "alpha"},
      {2U, "bravo"},
  };
  for (const auto& [player_id, player_name] : players) {
    static_cast<void>(player_name);
    movement.ensure_player(player_id);
    combat.ensure_player(player_id);
  }

  REQUIRE(combat.enqueue_fire({1U, 1U, "launcher", -20.0F, 0.0F, 1.0F, 0.0F, t0}) ==
          FireEnqueueStatus::Accepted);

  uint64_t resolved_tick = 0U;
  nlohmann::json resolved_snapshot{};

  for (uint64_t step = 1U; step <= 12U; ++step) {
    const auto now =
        t0 + std::chrono::milliseconds(static_cast<int64_t>(step) * 100);
    const auto frames = loop.advance(now);
    REQUIRE(frames.size() == 1U);

    for (const auto& frame : frames) {
      nlohmann::json pending_events = nlohmann::json::array();
      std::vector<devy::net::Packet> broadcasts{};
      movement.apply_inputs(loop.tick_interval(), frame.inputs);
      const auto movement_states = movement.snapshot();
      combat.set_player_positions(movement_states);
      const auto combat_result = combat.resolve_tick(frame.tick, loop.tick_interval());
      append_combat_outputs(combat_result, pending_events, &broadcasts);

      if (frame.tick < 11U) {
        REQUIRE(broadcasts.empty());
        REQUIRE(pending_events.empty());
      }
      if (frame.tick == 11U) {
        REQUIRE(broadcasts.size() == 1U);
        REQUIRE(broadcasts.front().payload["lethal"] == true);
        REQUIRE(pending_events.size() == 2U);
        resolved_tick = frame.tick;
        resolved_snapshot = build_snapshot_payload(frame.tick, players, movement_states,
                                                   combat.snapshot(), pending_events);
      }
    }
  }

  REQUIRE(resolved_tick == 11U);
  const auto* attacker = find_player(resolved_snapshot, 1U);
  REQUIRE(attacker != nullptr);
  REQUIRE((*attacker)["last_shot_seq"] == 1U);

  const auto* victim = find_player(resolved_snapshot, 2U);
  REQUIRE(victim != nullptr);
  REQUIRE((*victim)["health"] == 0);
  REQUIRE((*victim)["alive"] == false);
}

} // namespace
} // namespace devy::server
