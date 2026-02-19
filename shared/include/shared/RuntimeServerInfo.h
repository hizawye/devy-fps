#pragma once

#include <cstdint>
#include <optional>
#include <string>

namespace devy::runtime {

struct ServerEndpointInfo {
  std::string host{"127.0.0.1"};
  uint16_t port{0U};
  uint64_t pid{0U};
  std::string config_path{};
  uint64_t started_at_unix_ms{0U};
};

bool write_active_server_info(const std::string& output_path, const ServerEndpointInfo& info,
                              std::string* error = nullptr);
std::optional<ServerEndpointInfo> read_active_server_info(const std::string& input_path,
                                                          std::string* error = nullptr);
bool remove_active_server_info(const std::string& input_path, std::string* error = nullptr);

} // namespace devy::runtime
