#include "server/SessionManager.h"

#include <catch2/catch_test_macros.hpp>

#include <chrono>
#include <cstdint>
#include <string>

namespace devy::server {
namespace {

TEST_CASE("Session manager allocates slots and enforces capacity") {
  SessionManager sessions({2, std::chrono::milliseconds(1000)});
  const auto now = SessionClock::time_point{};

  const JoinResult first = sessions.handle_join_request(101U, "alpha", now);
  REQUIRE(first.accepted());
  REQUIRE(first.session.player_id == 1U);
  REQUIRE(sessions.active_count() == 1U);

  const JoinResult second = sessions.handle_join_request(102U, "bravo", now);
  REQUIRE(second.accepted());
  REQUIRE(second.session.player_id == 2U);
  REQUIRE(sessions.active_count() == 2U);

  const JoinResult third = sessions.handle_join_request(103U, "charlie", now);
  REQUIRE_FALSE(third.accepted());
  REQUIRE(third.error == JoinError::ServerFull);
}

TEST_CASE("Session manager disconnect cleanup frees slot and supports identity reconnect") {
  SessionManager sessions({2, std::chrono::milliseconds(1000)});
  const auto now = SessionClock::time_point{};

  const JoinResult first = sessions.handle_join_request(201U, "alpha", now);
  REQUIRE(first.accepted());
  REQUIRE(first.status == JoinStatus::Accepted);
  REQUIRE(first.session.player_id == 1U);

  auto disconnected = sessions.handle_disconnect(201U);
  REQUIRE(disconnected.has_value());
  REQUIRE(disconnected->player_id == 1U);
  REQUIRE(sessions.active_count() == 0U);

  const JoinResult reconnected =
    sessions.handle_join_request(301U, "alpha", now + std::chrono::milliseconds(50));
  REQUIRE(reconnected.accepted());
  REQUIRE(reconnected.status == JoinStatus::Reconnected);
  REQUIRE(reconnected.session.player_id == 1U);
  REQUIRE(sessions.active_count() == 1U);
}

TEST_CASE("Session manager rejects duplicate active identity") {
  SessionManager sessions({4, std::chrono::milliseconds(1000)});
  const auto now = SessionClock::time_point{};

  const JoinResult first = sessions.handle_join_request(1001U, "alpha", now);
  REQUIRE(first.accepted());

  const JoinResult duplicate = sessions.handle_join_request(1002U, "alpha", now);
  REQUIRE_FALSE(duplicate.accepted());
  REQUIRE(duplicate.error == JoinError::NameInUse);
}

TEST_CASE("Session manager heartbeat prevents timeout until stale threshold") {
  SessionManager sessions({2, std::chrono::milliseconds(100)});
  const auto t0 = SessionClock::time_point{};

  const JoinResult joined = sessions.handle_join_request(401U, "alpha", t0);
  REQUIRE(joined.accepted());

  REQUIRE(sessions.handle_heartbeat(401U, t0 + std::chrono::milliseconds(50)));
  REQUIRE_FALSE(sessions.handle_heartbeat(999U, t0 + std::chrono::milliseconds(50)));

  auto timed_out = sessions.collect_timed_out(t0 + std::chrono::milliseconds(120));
  REQUIRE(timed_out.empty());

  timed_out = sessions.collect_timed_out(t0 + std::chrono::milliseconds(151));
  REQUIRE(timed_out.size() == 1U);
  REQUIRE(timed_out.front().player_name == "alpha");
  REQUIRE(sessions.active_count() == 0U);
}

TEST_CASE("Session manager handles connect storm up to configured max players") {
  SessionManager sessions({64, std::chrono::milliseconds(1000)});
  const auto now = SessionClock::time_point{};

  for (std::uintptr_t peer = 1; peer <= 64; ++peer) {
    const JoinResult joined = sessions.handle_join_request(peer, "player_" + std::to_string(peer), now);
    REQUIRE(joined.accepted());
    REQUIRE(joined.session.player_id == static_cast<uint32_t>(peer));
  }

  const JoinResult overflow = sessions.handle_join_request(65U, "overflow", now);
  REQUIRE_FALSE(overflow.accepted());
  REQUIRE(overflow.error == JoinError::ServerFull);
}

TEST_CASE("Session manager returns active sessions ordered by player id") {
  SessionManager sessions({4, std::chrono::milliseconds(1000)});
  const auto now = SessionClock::time_point{};

  REQUIRE(sessions.handle_join_request(5001U, "charlie", now).accepted());
  REQUIRE(sessions.handle_join_request(5002U, "alpha", now).accepted());
  REQUIRE(sessions.handle_join_request(5003U, "bravo", now).accepted());
  REQUIRE(sessions.handle_disconnect(5002U).has_value());
  REQUIRE(sessions.handle_join_request(5004U, "alpha", now).accepted());

  const auto active = sessions.active_sessions();
  REQUIRE(active.size() == 3U);
  REQUIRE(active[0].player_name == "charlie");
  REQUIRE(active[0].player_id == 1U);
  REQUIRE(active[1].player_name == "alpha");
  REQUIRE(active[1].player_id == 2U);
  REQUIRE(active[2].player_name == "bravo");
  REQUIRE(active[2].player_id == 3U);
}

} // namespace
} // namespace devy::server
