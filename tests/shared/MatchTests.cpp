#include "shared/game/Match.h"

#include <catch2/catch_test_macros.hpp>

namespace devy::game {
namespace {

TEST_CASE("Match timer decrements only on full second boundaries") {
  MatchTimer timer;
  timer.start(3);

  timer.tick(0.4f);
  REQUIRE(timer.state().time_remaining_seconds == 3);
  REQUIRE(timer.state().running);

  timer.tick(0.6f);
  REQUIRE(timer.state().time_remaining_seconds == 2);
  REQUIRE(timer.state().running);
}

TEST_CASE("Match timer stops at zero") {
  MatchTimer timer;
  timer.start(2);

  timer.tick(5.0f);
  REQUIRE(timer.state().time_remaining_seconds == 0);
  REQUIRE_FALSE(timer.state().running);
}

} // namespace
} // namespace devy::game
