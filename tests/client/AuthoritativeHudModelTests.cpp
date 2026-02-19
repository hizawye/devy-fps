#include "client/AuthoritativeHudModel.h"

#include <catch2/catch_test_macros.hpp>

#include <string>

namespace devy::client {
namespace {

TEST_CASE("Authoritative HUD model updates from local snapshot player and match state") {
  AuthoritativeHudModel hud{};
  const nlohmann::json snapshot = {
      {"tick", 10},
      {"players",
       nlohmann::json::array({{{"player_id", 2U},
                               {"health", 70},
                               {"alive", true},
                               {"last_shot_seq", 12U},
                               {"coins", 140},
                               {"inventory_items", 4U}},
                              {{"player_id", 3U},
                               {"health", 99},
                               {"alive", true},
                               {"last_shot_seq", 2U},
                               {"coins", 10},
                               {"inventory_items", 1U}}})},
      {"match_state", {{"state", "in_match"}, {"remaining_seconds", 88.9}}}};

  REQUIRE(hud.apply_snapshot(snapshot, 2U));
  const auto& state = hud.state();
  REQUIRE(state.has_player_state);
  REQUIRE(state.health == 70);
  REQUIRE(state.alive);
  REQUIRE(state.last_shot_seq == 12U);
  REQUIRE(state.coins == 140);
  REQUIRE(state.inventory_items == 4U);
  REQUIRE(state.match_state == "in_match");
  REQUIRE(state.match_remaining_seconds == 88);

  REQUIRE_FALSE(hud.apply_snapshot(snapshot, 2U));
}

TEST_CASE("Authoritative HUD model applies inventory, match, and damage packets") {
  AuthoritativeHudModel hud{};

  SECTION("inventory update only applies for local player") {
    const nlohmann::json foreign = {{"player_id", 9U}, {"coins", 5}, {"item_count", 1U}};
    REQUIRE_FALSE(hud.apply_inventory_update(foreign, 2U));

    const nlohmann::json local = {{"player_id", 2U}, {"coins", 77}, {"item_count", 3U}};
    REQUIRE(hud.apply_inventory_update(local, 2U));
    REQUIRE(hud.state().has_player_state);
    REQUIRE(hud.state().coins == 77);
    REQUIRE(hud.state().inventory_items == 3U);
  }

  SECTION("match state update floors remaining time") {
    const nlohmann::json match = {{"state", "pre_match"}, {"remaining_seconds", 4.99}};
    REQUIRE(hud.apply_match_state(match));
    REQUIRE(hud.state().match_state == "pre_match");
    REQUIRE(hud.state().match_remaining_seconds == 4);
  }

  SECTION("damage event updates local health and alive state") {
    const nlohmann::json damage = {
        {"victim_id", 2U}, {"damage", 25}, {"victim_health", 45}, {"lethal", false}};
    REQUIRE(hud.apply_damage_event(damage, 2U));
    REQUIRE(hud.state().has_player_state);
    REQUIRE(hud.state().health == 45);
    REQUIRE(hud.state().alive);

    const nlohmann::json lethal = {
        {"victim_id", 2U}, {"damage", 45}, {"victim_health", 0}, {"lethal", true}};
    REQUIRE(hud.apply_damage_event(lethal, 2U));
    REQUIRE(hud.state().health == 0);
    REQUIRE_FALSE(hud.state().alive);
  }
}

TEST_CASE("Authoritative HUD model composes readable window title") {
  AuthoritativeHudModel hud{};
  std::string title = hud.compose_window_title("Devy FPS Client", "rifle");
  REQUIRE(title.find("HP ?") != std::string::npos);
  REQUIRE(title.find("Weapon rifle") != std::string::npos);
  REQUIRE(title.find("Match unknown ?") != std::string::npos);

  const nlohmann::json snapshot = {
      {"players",
       nlohmann::json::array({{{"player_id", 1U},
                               {"health", 91},
                               {"alive", false},
                               {"last_shot_seq", 18U},
                               {"coins", 250},
                               {"inventory_items", 7U}}})},
      {"match_state", {{"state", "post_match"}, {"remaining_seconds", 15.0}}}};
  REQUIRE(hud.apply_snapshot(snapshot, 1U));
  title = hud.compose_window_title("Devy FPS Client", "railgun");
  REQUIRE(title.find("HP 91 (down)") != std::string::npos);
  REQUIRE(title.find("Weapon railgun") != std::string::npos);
  REQUIRE(title.find("ShotSeq 18") != std::string::npos);
  REQUIRE(title.find("Coins 250") != std::string::npos);
  REQUIRE(title.find("Items 7") != std::string::npos);
  REQUIRE(title.find("Match post_match 15s") != std::string::npos);
}

} // namespace
} // namespace devy::client
