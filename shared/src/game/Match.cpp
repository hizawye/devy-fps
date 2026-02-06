#include "shared/game/Match.h"

namespace devy::game {

void MatchTimer::start(int duration_seconds) {
  state_.time_remaining_seconds = duration_seconds;
  state_.running = true;
  accumulator_ = 0.0f;
}

void MatchTimer::tick(float delta_seconds) {
  if (!state_.running) {
    return;
  }

  accumulator_ += delta_seconds;
  if (accumulator_ >= 1.0f) {
    int ticks = static_cast<int>(accumulator_);
    accumulator_ -= static_cast<float>(ticks);
    state_.time_remaining_seconds -= ticks;
    if (state_.time_remaining_seconds <= 0) {
      state_.time_remaining_seconds = 0;
      state_.running = false;
    }
  }
}

const MatchState& MatchTimer::state() const {
  return state_;
}

} // namespace devy::game
