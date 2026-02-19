#include "shared/RuntimeServerInfo.h"

#include <filesystem>
#include <fstream>
#include <limits>
#include <optional>

#include <nlohmann/json.hpp>

namespace devy::runtime {
namespace {

bool set_error(std::string* error, const std::string& message) {
  if (error != nullptr) {
    *error = message;
  }
  return false;
}

std::optional<uint16_t> parse_port(const nlohmann::json& value) {
  if (value.is_number_unsigned()) {
    const auto raw = value.get<uint64_t>();
    if (raw >= 1U && raw <= static_cast<uint64_t>(std::numeric_limits<uint16_t>::max())) {
      return static_cast<uint16_t>(raw);
    }
    return std::nullopt;
  }
  if (value.is_number_integer()) {
    const auto raw = value.get<int64_t>();
    if (raw >= 1 && raw <= static_cast<int64_t>(std::numeric_limits<uint16_t>::max())) {
      return static_cast<uint16_t>(raw);
    }
    return std::nullopt;
  }
  return std::nullopt;
}

std::optional<uint64_t> parse_u64(const nlohmann::json& value) {
  if (value.is_number_unsigned()) {
    return value.get<uint64_t>();
  }
  if (value.is_number_integer()) {
    const auto raw = value.get<int64_t>();
    if (raw >= 0) {
      return static_cast<uint64_t>(raw);
    }
  }
  return std::nullopt;
}

} // namespace

bool write_active_server_info(const std::string& output_path, const ServerEndpointInfo& info,
                              std::string* error) {
  if (output_path.empty()) {
    return set_error(error, "active server info path is empty");
  }
  if (info.host.empty()) {
    return set_error(error, "active server info host is empty");
  }
  if (info.port == 0U) {
    return set_error(error, "active server info port must be non-zero");
  }

  const std::filesystem::path path(output_path);
  const std::filesystem::path parent = path.parent_path();
  if (!parent.empty()) {
    std::error_code mkdir_error{};
    std::filesystem::create_directories(parent, mkdir_error);
    if (mkdir_error) {
      return set_error(error,
                       "failed to create directory `" + parent.string() +
                           "`: " + mkdir_error.message());
    }
  }

  const nlohmann::json payload = {
      {"host", info.host},
      {"port", info.port},
      {"pid", info.pid},
      {"config_path", info.config_path},
      {"started_at_unix_ms", info.started_at_unix_ms},
  };

  const std::filesystem::path tmp_path = path.string() + ".tmp";
  std::ofstream out(tmp_path, std::ios::binary | std::ios::trunc);
  if (!out.is_open()) {
    return set_error(error, "failed to open temporary file `" + tmp_path.string() + "`");
  }
  out << payload.dump();
  out << '\n';
  out.flush();
  if (!out.good()) {
    out.close();
    std::error_code cleanup_error{};
    std::filesystem::remove(tmp_path, cleanup_error);
    return set_error(error, "failed to write temporary file `" + tmp_path.string() + "`");
  }
  out.close();

  std::error_code rename_error{};
  std::filesystem::rename(tmp_path, path, rename_error);
  if (rename_error) {
    std::error_code remove_error{};
    std::filesystem::remove(path, remove_error);
    rename_error.clear();
    std::filesystem::rename(tmp_path, path, rename_error);
    if (rename_error) {
      std::error_code cleanup_error{};
      std::filesystem::remove(tmp_path, cleanup_error);
      return set_error(error,
                       "failed to publish active server info file `" + path.string() +
                           "`: " + rename_error.message());
    }
  }

  return true;
}

std::optional<ServerEndpointInfo> read_active_server_info(const std::string& input_path,
                                                          std::string* error) {
  if (input_path.empty()) {
    set_error(error, "active server info path is empty");
    return std::nullopt;
  }

  std::ifstream in(input_path);
  if (!in.is_open()) {
    set_error(error, "failed to open active server info file `" + input_path + "`");
    return std::nullopt;
  }

  nlohmann::json payload{};
  try {
    in >> payload;
  } catch (const std::exception& e) {
    set_error(error, "failed to parse active server info JSON: " + std::string(e.what()));
    return std::nullopt;
  }
  if (!payload.is_object()) {
    set_error(error, "active server info payload must be an object");
    return std::nullopt;
  }

  if (!payload.contains("host") || !payload["host"].is_string()) {
    set_error(error, "active server info is missing string `host`");
    return std::nullopt;
  }
  const std::string host = payload["host"].get<std::string>();
  if (host.empty()) {
    set_error(error, "active server info `host` must not be empty");
    return std::nullopt;
  }

  if (!payload.contains("port")) {
    set_error(error, "active server info is missing `port`");
    return std::nullopt;
  }
  const auto parsed_port = parse_port(payload["port"]);
  if (!parsed_port.has_value()) {
    set_error(error, "active server info `port` must be in [1,65535]");
    return std::nullopt;
  }

  ServerEndpointInfo out{};
  out.host = host;
  out.port = parsed_port.value();

  if (payload.contains("pid")) {
    const auto parsed_pid = parse_u64(payload["pid"]);
    if (!parsed_pid.has_value()) {
      set_error(error, "active server info `pid` must be an unsigned integer");
      return std::nullopt;
    }
    out.pid = parsed_pid.value();
  }

  if (payload.contains("config_path")) {
    if (!payload["config_path"].is_string()) {
      set_error(error, "active server info `config_path` must be a string");
      return std::nullopt;
    }
    out.config_path = payload["config_path"].get<std::string>();
  }

  if (payload.contains("started_at_unix_ms")) {
    const auto parsed_started = parse_u64(payload["started_at_unix_ms"]);
    if (!parsed_started.has_value()) {
      set_error(error, "active server info `started_at_unix_ms` must be an unsigned integer");
      return std::nullopt;
    }
    out.started_at_unix_ms = parsed_started.value();
  }

  if (error != nullptr) {
    error->clear();
  }
  return out;
}

bool remove_active_server_info(const std::string& input_path, std::string* error) {
  if (input_path.empty()) {
    return set_error(error, "active server info path is empty");
  }

  std::error_code remove_error{};
  const bool removed = std::filesystem::remove(input_path, remove_error);
  if (remove_error) {
    return set_error(error, "failed to remove active server info file `" + input_path +
                                "`: " + remove_error.message());
  }
  if (error != nullptr) {
    error->clear();
  }
  return removed || !std::filesystem::exists(input_path);
}

} // namespace devy::runtime
