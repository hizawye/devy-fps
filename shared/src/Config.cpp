#include "shared/Config.h"

#include "shared/Log.h"

#include <fstream>
#include <sstream>

namespace devy::config {

std::optional<nlohmann::json> try_load_json(const std::string& path) {
  std::ifstream file(path);
  if (!file.is_open()) {
    devy::log::write(devy::log::Level::Error, "Failed to open config: " + path);
    return std::nullopt;
  }

  std::stringstream buffer;
  buffer << file.rdbuf();

  try {
    nlohmann::json parsed = nlohmann::json::parse(buffer.str());
    return std::optional<nlohmann::json>{std::move(parsed)};
  } catch (const std::exception& e) {
    devy::log::write(devy::log::Level::Error, std::string("Failed to parse JSON: ") + e.what());
    return std::nullopt;
  }
}

nlohmann::json load_json(const std::string& path) {
  const auto parsed = try_load_json(path);
  if (!parsed.has_value()) {
    return nlohmann::json{};
  }
  return parsed.value();
}

} // namespace devy::config
