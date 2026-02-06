#pragma once

#include <nlohmann/json.hpp>
#include <string>

namespace devy::config {

nlohmann::json load_json(const std::string& path);

} // namespace devy::config
