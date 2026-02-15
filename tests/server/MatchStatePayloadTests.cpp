#include "server/MatchStatePayload.h"

#include <catch2/catch_test_macros.hpp>

namespace devy::server {
namespace {

TEST_CASE("Match state payload includes scoreboard when requested") {
  const MatchLifecycleState state{MatchPhase::InMatch, 42U, 1000U, std::nullopt};
  const std::vector<MatchScoreEntry> scoreboard = {
      {1U, 4U, 2U, 25, 1U, true, false, true},
      {2U, 3U, 3U, 20, 0U, false, true, true}};

  const nlohmann::json payload = build_match_state_payload(state, scoreboard, true);

  REQUIRE(payload.contains("state"));
  REQUIRE(payload.contains("remaining_seconds"));
  REQUIRE(payload.contains("phase_started_tick"));
  REQUIRE(payload.contains("scoreboard"));
  REQUIRE(payload["scoreboard"].is_array());
  REQUIRE(payload["scoreboard"].size() == scoreboard.size());
}

TEST_CASE("Match state payload omits scoreboard when snapshot knob is disabled") {
  const MatchLifecycleState state{MatchPhase::PostMatch, 0U, 2000U, 1U};
  const std::vector<MatchScoreEntry> scoreboard = {
      {1U, 5U, 1U, 30, 0U, true, false, true}};

  const nlohmann::json payload = build_match_state_payload(state, scoreboard, false);

  REQUIRE(payload.contains("state"));
  REQUIRE(payload.contains("remaining_seconds"));
  REQUIRE(payload.contains("phase_started_tick"));
  REQUIRE(payload.contains("winner_player_id"));
  REQUIRE_FALSE(payload.contains("scoreboard"));
}

TEST_CASE("Reliable match_state packet always includes scoreboard") {
  const MatchLifecycleState state{MatchPhase::InMatch, 10U, 3000U, std::nullopt};
  const std::vector<MatchScoreEntry> scoreboard = {
      {7U, 1U, 0U, 5, 2U, true, false, true}};

  const auto packet = build_match_state_packet(state, scoreboard);

  REQUIRE(packet.type == devy::net::MessageType::MatchState);
  REQUIRE(packet.payload.contains("scoreboard"));
  REQUIRE(packet.payload["scoreboard"].is_array());
  REQUIRE(packet.payload["scoreboard"].size() == 1U);
}

} // namespace
} // namespace devy::server
