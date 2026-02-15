#include "server/AuthoritativeLoop.h"

#include <algorithm>
#include <utility>

namespace devy::server {

AuthoritativeLoop::AuthoritativeLoop(RuntimeConfig config, RuntimeTimePoint start_time)
  : config_(sanitize_config(config)) {
  tick_interval_ = std::chrono::nanoseconds(1'000'000'000LL) / static_cast<int64_t>(config_.tick_rate_hz);
  if (tick_interval_ <= std::chrono::nanoseconds::zero()) {
    tick_interval_ = std::chrono::nanoseconds(1);
  }
  reset(start_time);
}

void AuthoritativeLoop::reset(RuntimeTimePoint start_time) {
  tick_counter_ = 0;
  pending_input_count_ = 0;
  input_queues_by_player_.clear();
  last_input_seq_by_player_.clear();
  next_tick_deadline_ = start_time + tick_interval_;
}

InputEnqueueStatus AuthoritativeLoop::enqueue_input(const PlayerInputCommand& input) {
  auto last_seq = last_input_seq_by_player_.find(input.player_id);
  if (last_seq != last_input_seq_by_player_.end() && input.input_seq <= last_seq->second) {
    return InputEnqueueStatus::OutOfOrder;
  }

  auto queue_it = input_queues_by_player_.find(input.player_id);
  if (queue_it != input_queues_by_player_.end() && !queue_it->second.empty()) {
    if (input.input_seq <= queue_it->second.back().input_seq) {
      return InputEnqueueStatus::OutOfOrder;
    }
  }

  if (pending_input_count_ >= config_.input_queue_capacity) {
    return InputEnqueueStatus::QueueFull;
  }

  input_queues_by_player_[input.player_id].push_back(input);
  last_input_seq_by_player_[input.player_id] = input.input_seq;
  ++pending_input_count_;
  return InputEnqueueStatus::Accepted;
}

void AuthoritativeLoop::clear_player(uint32_t player_id) {
  auto queue_it = input_queues_by_player_.find(player_id);
  if (queue_it != input_queues_by_player_.end()) {
    pending_input_count_ -= queue_it->second.size();
    input_queues_by_player_.erase(queue_it);
  }
  last_input_seq_by_player_.erase(player_id);
}

std::vector<TickFrame> AuthoritativeLoop::advance(RuntimeTimePoint now) {
  std::vector<TickFrame> frames{};
  while (now >= next_tick_deadline_) {
    TickFrame frame{};
    ++tick_counter_;
    frame.tick = tick_counter_;
    frame.inputs = drain_inputs_deterministic();
    frame.snapshot_due = (frame.tick % config_.snapshot_interval_ticks) == 0;
    frames.push_back(std::move(frame));
    next_tick_deadline_ += tick_interval_;
  }
  return frames;
}

uint64_t AuthoritativeLoop::last_completed_tick() const {
  return tick_counter_;
}

RuntimeTimePoint AuthoritativeLoop::next_tick_deadline() const {
  return next_tick_deadline_;
}

std::size_t AuthoritativeLoop::pending_input_count() const {
  return pending_input_count_;
}

std::chrono::nanoseconds AuthoritativeLoop::tick_interval() const {
  return tick_interval_;
}

RuntimeConfig AuthoritativeLoop::sanitize_config(RuntimeConfig config) {
  if (config.tick_rate_hz == 0U) {
    config.tick_rate_hz = 30U;
  }
  if (config.input_queue_capacity == 0U) {
    config.input_queue_capacity = 1U;
  }
  if (config.snapshot_interval_ticks == 0U) {
    config.snapshot_interval_ticks = 1U;
  }
  return config;
}

std::vector<PlayerInputCommand> AuthoritativeLoop::drain_inputs_deterministic() {
  std::vector<uint32_t> player_ids{};
  player_ids.reserve(input_queues_by_player_.size());
  for (const auto& [player_id, queue] : input_queues_by_player_) {
    if (!queue.empty()) {
      player_ids.push_back(player_id);
    }
  }
  std::sort(player_ids.begin(), player_ids.end());

  std::vector<PlayerInputCommand> drained{};
  drained.reserve(pending_input_count_);
  for (uint32_t player_id : player_ids) {
    auto queue_it = input_queues_by_player_.find(player_id);
    if (queue_it == input_queues_by_player_.end()) {
      continue;
    }
    auto& queue = queue_it->second;
    while (!queue.empty()) {
      drained.push_back(queue.front());
      queue.pop_front();
      --pending_input_count_;
    }
    input_queues_by_player_.erase(queue_it);
  }
  return drained;
}

const char* to_string(InputEnqueueStatus status) {
  switch (status) {
    case InputEnqueueStatus::Accepted: return "accepted";
    case InputEnqueueStatus::QueueFull: return "queue_full";
    case InputEnqueueStatus::OutOfOrder: return "out_of_order";
    default: return "unknown_input_enqueue_status";
  }
}

} // namespace devy::server
