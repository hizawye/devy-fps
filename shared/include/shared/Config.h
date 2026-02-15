#pragma once

#include <nlohmann/json.hpp>
#include <optional>
#include <string>

namespace devy::config {

std::optional<nlohmann::json> try_load_json(const std::string& path);
nlohmann::json load_json(const std::string& path);

} // namespace devy::config
