#pragma once

namespace devy::game {

struct MatchState {
  int time_remaining_seconds = 0;
  bool running = false;
};

class MatchTimer {
public:
  void start(int duration_seconds);
  void tick(float delta_seconds);
  const MatchState& state() const;

private:
  MatchState state_{};
  float accumulator_ = 0.0f;
};

} // namespace devy::game
