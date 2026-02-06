#include "shared/Log.h"

#include <iostream>
#include <mutex>

namespace devy::log {

namespace {
std::mutex log_mutex;

const char* level_label(Level level) {
  switch (level) {
    case Level::Info: return "INFO";
    case Level::Warn: return "WARN";
    case Level::Error: return "ERROR";
    default: return "LOG";
  }
}
}

void write(Level level, const std::string& message) {
  std::lock_guard<std::mutex> lock(log_mutex);
  std::ostream& out = (level == Level::Error) ? std::cerr : std::cout;
  out << "[" << level_label(level) << "] " << message << '\n';
}

} // namespace devy::log
