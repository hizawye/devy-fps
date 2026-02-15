#pragma once

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <deque>
#include <unordered_map>
#include <vector>

namespace devy::server {

using RuntimeClock = std::chrono::steady_clock;
using RuntimeTimePoint = RuntimeClock::time_point;

struct RuntimeConfig {
  uint32_t tick_rate_hz{30};
  std::size_t input_queue_capacity{2048};
  uint32_t snapshot_interval_ticks{2};
};

struct PlayerInputCommand {
  uint32_t player_id{0};
  uint32_t input_seq{0};
  float move_x{0.0F};
  float move_y{0.0F};
  bool jump{false};
  bool fire{false};
  RuntimeTimePoint received_at{};
};

enum class InputEnqueueStatus : uint8_t {
  Accepted = 0,
  QueueFull,
  OutOfOrder
};

struct TickFrame {
  uint64_t tick{0};
  std::vector<PlayerInputCommand> inputs{};
  bool snapshot_due{false};
};

class AuthoritativeLoop {
 public:
  explicit AuthoritativeLoop(RuntimeConfig config, RuntimeTimePoint start_time = RuntimeTimePoint{});

  void reset(RuntimeTimePoint start_time);
  InputEnqueueStatus enqueue_input(const PlayerInputCommand& input);
  void clear_player(uint32_t player_id);
  std::vector<TickFrame> advance(RuntimeTimePoint now);

  [[nodiscard]] uint64_t last_completed_tick() const;
  [[nodiscard]] RuntimeTimePoint next_tick_deadline() const;
  [[nodiscard]] std::size_t pending_input_count() const;
  [[nodiscard]] std::chrono::nanoseconds tick_interval() const;

 private:
  [[nodiscard]] static RuntimeConfig sanitize_config(RuntimeConfig config);
  [[nodiscard]] std::vector<PlayerInputCommand> drain_inputs_deterministic();

  RuntimeConfig config_;
  std::chrono::nanoseconds tick_interval_{};
  RuntimeTimePoint next_tick_deadline_{};
  uint64_t tick_counter_{0};
  std::size_t pending_input_count_{0};
  std::unordered_map<uint32_t, std::deque<PlayerInputCommand>> input_queues_by_player_{};
  std::unordered_map<uint32_t, uint32_t> last_input_seq_by_player_{};
};

const char* to_string(InputEnqueueStatus status);

} // namespace devy::server
