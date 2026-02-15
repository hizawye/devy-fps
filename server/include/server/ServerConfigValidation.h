#pragma once

#include <nlohmann/json.hpp>

#include <string>
#include <vector>

namespace devy::server {

std::vector<std::string> validate_server_config(const nlohmann::json& config);

} // namespace devy::server
