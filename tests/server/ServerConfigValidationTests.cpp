#include "server/ServerConfigValidation.h"

#include <catch2/catch_test_macros.hpp>

namespace devy::server {
namespace {

TEST_CASE("Server config validation accepts empty object and valid optional fields") {
  const nlohmann::json config = {
      {"max_players", 16},
      {"port", 7777},
      {"loot_drop", "none"},
      {"runtime", {{"tick_rate_hz", 30}, {"snapshot_interval_ticks", 2}}},
      {"map", {{"chunks_x", 8}, {"chunks_z", 8}, {"world_height", 128}, {"draw_distance_chunks", 2}}}};

  const auto errors = validate_server_config(config);
  REQUIRE(errors.empty());
}

TEST_CASE("Server config validation rejects invalid root type and field types") {
  const auto root_errors = validate_server_config(nlohmann::json::array());
  REQUIRE(root_errors.size() == 1U);
  REQUIRE(root_errors.front().find("<root>") != std::string::npos);

  const nlohmann::json wrong_types = {
      {"max_players", "lots"},
      {"runtime", {{"match_state_broadcast_on_snapshot", "yes"}}}};
  const auto type_errors = validate_server_config(wrong_types);
  REQUIRE(type_errors.size() == 2U);
  REQUIRE(type_errors.at(0).find("max_players") != std::string::npos);
  REQUIRE(type_errors.at(1).find("runtime.match_state_broadcast_on_snapshot") !=
          std::string::npos);
}

TEST_CASE("Server config validation rejects invalid runtime ranges and enum strings") {
  const nlohmann::json invalid = {
      {"loot_drop", "drop_everything"},
      {"runtime",
       {{"tick_rate_hz", 0},
        {"snapshot_interval_ticks", -1},
        {"profiling", {{"report_interval_ticks", 20}, {"history_size_ticks", 10}}}}},
      {"match", {{"duration_seconds", 0}, {"respawns_per_player", -2}}},
      {"map", {{"draw_distance_chunks", -1}}}};

  const auto errors = validate_server_config(invalid);
  REQUIRE(errors.size() == 7U);
  REQUIRE(errors.at(0).find("loot_drop") != std::string::npos);
  REQUIRE(errors.at(1).find("runtime.tick_rate_hz") != std::string::npos);
  REQUIRE(errors.at(2).find("runtime.snapshot_interval_ticks") != std::string::npos);
  REQUIRE(errors.at(3).find("runtime.profiling.history_size_ticks") != std::string::npos);
  REQUIRE(errors.at(4).find("match.duration_seconds") != std::string::npos);
  REQUIRE(errors.at(5).find("match.respawns_per_player") != std::string::npos);
  REQUIRE(errors.at(6).find("map.draw_distance_chunks") != std::string::npos);
}

TEST_CASE("Server config validation rejects invalid profiling diagnostic thresholds") {
  const nlohmann::json invalid = {
      {"runtime",
       {{"profiling",
         {{"tick_lag_tolerance_ms", -0.1},
          {"alerts",
           {{"max_tick_lag_rate", 1.5},
            {"max_packet_drop_rate", -0.1},
            {"max_parse_error_rate", 2.0},
            {"min_active_players", -1}}}}}}}};

  const auto errors = validate_server_config(invalid);
  REQUIRE(errors.size() == 5U);
  REQUIRE(errors.at(0).find("runtime.profiling.tick_lag_tolerance_ms") != std::string::npos);
  REQUIRE(errors.at(1).find("runtime.profiling.alerts.max_tick_lag_rate") != std::string::npos);
  REQUIRE(errors.at(2).find("runtime.profiling.alerts.max_packet_drop_rate") !=
          std::string::npos);
  REQUIRE(errors.at(3).find("runtime.profiling.alerts.max_parse_error_rate") !=
          std::string::npos);
  REQUIRE(errors.at(4).find("runtime.profiling.alerts.min_active_players") !=
          std::string::npos);
}

} // namespace
} // namespace devy::server
