#include "shared/Config.h"

#include "shared/Log.h"

#include <fstream>
#include <sstream>

namespace devy::config {

nlohmann::json load_json(const std::string& path) {
  std::ifstream file(path);
  if (!file.is_open()) {
    devy::log::write(devy::log::Level::Error, "Failed to open config: " + path);
    return nlohmann::json{};
  }

  std::stringstream buffer;
  buffer << file.rdbuf();

  try {
    return nlohmann::json::parse(buffer.str());
  } catch (const std::exception& e) {
    devy::log::write(devy::log::Level::Error, std::string("Failed to parse JSON: ") + e.what());
    return nlohmann::json{};
  }
}

} // namespace devy::config
