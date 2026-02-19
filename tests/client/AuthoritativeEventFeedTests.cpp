#include "client/AuthoritativeEventFeed.h"

#include <catch2/catch_test_macros.hpp>

namespace devy::client {
namespace {

TEST_CASE("Authoritative event feed reports local treasure pickup outcomes from snapshot events") {
  AuthoritativeEventFeed feed{};
  const nlohmann::json snapshot = {
      {"events",
       nlohmann::json::array(
           {{{"type", "treasure_pickup"},
             {"player_id", 7U},
             {"status", "collected"},
             {"treasure_id", "relic"},
             {"total_value", 120},
             {"item_count", 3}},
            {{"type", "treasure_pickup"}, {"player_id", 8U}, {"status", "collected"}},
            {{"type", "treasure_pickup"},
             {"player_id", 7U},
             {"status", "inventory_capacity_exceeded"}}})}};

  const auto messages = feed.consume_snapshot_events(snapshot, 7U);
  REQUIRE(messages.size() == 2U);
  REQUIRE(messages[0].find("Pickup collected: relic") != std::string::npos);
  REQUIRE(messages[1] == "Pickup failed: inventory full");
}

TEST_CASE("Authoritative event feed reports match phase transitions once") {
  AuthoritativeEventFeed feed{};

  const auto pre_match = feed.consume_reliable_match_state(
      {{"state", "pre_match"}, {"remaining_seconds", 5.9}});
  REQUIRE(pre_match.has_value());
  REQUIRE(pre_match.value() == "Phase: pre_match (5s)");

  const auto pre_match_repeat = feed.consume_reliable_match_state(
      {{"state", "pre_match"}, {"remaining_seconds", 4.1}});
  REQUIRE_FALSE(pre_match_repeat.has_value());

  const nlohmann::json snapshot = {
      {"events", nlohmann::json::array({{{"type", "match_state_changed"},
                                         {"state", "in_match"},
                                         {"remaining_seconds", 60.0}}})}};
  const auto snapshot_messages = feed.consume_snapshot_events(snapshot, 1U);
  REQUIRE(snapshot_messages.size() == 1U);
  REQUIRE(snapshot_messages[0] == "Phase: in_match (60s)");

  const auto in_match_repeat = feed.consume_reliable_match_state(
      {{"state", "in_match"}, {"remaining_seconds", 59.0}});
  REQUIRE_FALSE(in_match_repeat.has_value());
}

TEST_CASE("Authoritative event feed deduplicates reliable and snapshot damage confirmations") {
  AuthoritativeEventFeed feed{};

  const nlohmann::json reliable_hit = {{"attacker_id", 4U},
                                       {"victim_id", 5U},
                                       {"damage", 24},
                                       {"lethal", false},
                                       {"shot_seq", 11U},
                                       {"tick", 42U}};
  const auto first = feed.consume_reliable_damage_event(reliable_hit, 4U);
  REQUIRE(first.has_value());
  REQUIRE(first.value() == "Hit confirmed: 24");

  const nlohmann::json snapshot = {
      {"events", nlohmann::json::array({{{"type", "damage_event"},
                                         {"attacker_id", 4U},
                                         {"victim_id", 5U},
                                         {"damage", 24},
                                         {"lethal", false},
                                         {"shot_seq", 11U},
                                         {"tick", 42U}}})}};
  const auto messages = feed.consume_snapshot_events(snapshot, 4U);
  REQUIRE(messages.empty());

  const auto lethal_on_local = feed.consume_reliable_damage_event(
      {{"attacker_id", 6U},
       {"victim_id", 4U},
       {"damage", 80},
       {"lethal", true},
       {"shot_seq", 12U},
       {"tick", 43U}},
      4U);
  REQUIRE(lethal_on_local.has_value());
  REQUIRE(lethal_on_local.value() == "You were eliminated");
}

} // namespace
} // namespace devy::client
