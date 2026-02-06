#pragma once

#include <string>

namespace devy::log {

enum class Level {
  Info,
  Warn,
  Error
};

void write(Level level, const std::string& message);

} // namespace devy::log
