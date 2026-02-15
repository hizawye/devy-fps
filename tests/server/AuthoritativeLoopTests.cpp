#include "server/AuthoritativeLoop.h"

#include <catch2/catch_test_macros.hpp>

#include <chrono>
#include <vector>

namespace devy::server {
namespace {

TEST_CASE("Authoritative loop scheduler stays stable over 30 minute horizon") {
  const RuntimeTimePoint t0{};
  AuthoritativeLoop loop({60U, 4096U, 2U}, t0);

  std::size_t total_ticks = 0;
  for (int second = 1; second <= 1800; ++second) {
    const auto now = t0 + std::chrono::seconds(second);
    const auto frames = loop.advance(now);
    total_ticks += frames.size();
  }

  REQUIRE(total_ticks == 1800U * 60U);
  REQUIRE(loop.last_completed_tick() == total_ticks);

  const auto end_time = t0 + std::chrono::minutes(30);
  REQUIRE(loop.next_tick_deadline() > end_time);
  REQUIRE(loop.next_tick_deadline() - end_time <= loop.tick_interval());
}

TEST_CASE("Authoritative loop rejects input when queue capacity is exceeded") {
  const RuntimeTimePoint t0{};
  AuthoritativeLoop loop({20U, 2U, 2U}, t0);

  const PlayerInputCommand first{1U, 1U, 1.0F, 0.0F, false, false, t0};
  const PlayerInputCommand second{1U, 2U, 0.0F, 1.0F, false, true, t0};
  const PlayerInputCommand overflow{1U, 3U, 0.0F, 0.0F, true, false, t0};

  REQUIRE(loop.enqueue_input(first) == InputEnqueueStatus::Accepted);
  REQUIRE(loop.enqueue_input(second) == InputEnqueueStatus::Accepted);
  REQUIRE(loop.enqueue_input(overflow) == InputEnqueueStatus::QueueFull);
  REQUIRE(loop.pending_input_count() == 2U);

  const auto frames = loop.advance(t0 + std::chrono::seconds(1));
  REQUIRE_FALSE(frames.empty());
  REQUIRE(frames.front().inputs.size() == 2U);
  REQUIRE(loop.pending_input_count() == 0U);
}

TEST_CASE("Authoritative loop drains inputs in deterministic player/sequence order and snapshots on cadence") {
  const RuntimeTimePoint t0{};
  AuthoritativeLoop loop({10U, 64U, 3U}, t0);

  REQUIRE(loop.enqueue_input({2U, 10U, 1.0F, 0.0F, false, false, t0}) == InputEnqueueStatus::Accepted);
  REQUIRE(loop.enqueue_input({1U, 5U, 0.0F, 1.0F, false, true, t0}) == InputEnqueueStatus::Accepted);
  REQUIRE(loop.enqueue_input({2U, 11U, 0.5F, 0.5F, true, false, t0}) == InputEnqueueStatus::Accepted);
  REQUIRE(loop.enqueue_input({1U, 4U, 0.0F, 0.0F, false, false, t0}) == InputEnqueueStatus::OutOfOrder);

  const auto first_tick = loop.advance(t0 + std::chrono::milliseconds(100));
  REQUIRE(first_tick.size() == 1U);
  REQUIRE(first_tick.front().tick == 1U);
  REQUIRE_FALSE(first_tick.front().snapshot_due);
  REQUIRE(first_tick.front().inputs.size() == 3U);
  REQUIRE(first_tick.front().inputs[0].player_id == 1U);
  REQUIRE(first_tick.front().inputs[0].input_seq == 5U);
  REQUIRE(first_tick.front().inputs[1].player_id == 2U);
  REQUIRE(first_tick.front().inputs[1].input_seq == 10U);
  REQUIRE(first_tick.front().inputs[2].player_id == 2U);
  REQUIRE(first_tick.front().inputs[2].input_seq == 11U);

  std::vector<uint64_t> snapshot_ticks{};
  for (int tick = 2; tick <= 6; ++tick) {
    const auto now = t0 + std::chrono::milliseconds(100 * tick);
    const auto frames = loop.advance(now);
    REQUIRE(frames.size() == 1U);
    if (frames.front().snapshot_due) {
      snapshot_ticks.push_back(frames.front().tick);
    }
  }

  REQUIRE(snapshot_ticks == std::vector<uint64_t>{3U, 6U});
}

} // namespace
} // namespace devy::server
