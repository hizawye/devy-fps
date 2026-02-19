#include "shared/RuntimeServerInfo.h"

#include <catch2/catch_test_macros.hpp>

#include <chrono>
#include <filesystem>
#include <fstream>
#include <string>

namespace devy::runtime {
namespace {

std::filesystem::path unique_temp_dir() {
  const auto suffix = std::to_string(std::chrono::steady_clock::now().time_since_epoch().count());
  return std::filesystem::temp_directory_path() / ("devy-runtime-server-info-" + suffix);
}

TEST_CASE("Runtime server info round-trips through disk") {
  const std::filesystem::path temp_dir = unique_temp_dir();
  const std::filesystem::path output_path = temp_dir / "active-server.json";
  std::filesystem::create_directories(temp_dir);

  ServerEndpointInfo input{};
  input.host = "127.0.0.1";
  input.port = 7788U;
  input.pid = 4242U;
  input.config_path = "config/server_playable_fast.json";
  input.started_at_unix_ms = 1700000000123U;

  std::string write_error{};
  REQUIRE(write_active_server_info(output_path.string(), input, &write_error));
  REQUIRE(write_error.empty());
  REQUIRE(std::filesystem::exists(output_path));

  std::string read_error{};
  const auto loaded = read_active_server_info(output_path.string(), &read_error);
  REQUIRE(loaded.has_value());
  REQUIRE(read_error.empty());
  REQUIRE(loaded->host == input.host);
  REQUIRE(loaded->port == input.port);
  REQUIRE(loaded->pid == input.pid);
  REQUIRE(loaded->config_path == input.config_path);
  REQUIRE(loaded->started_at_unix_ms == input.started_at_unix_ms);

  std::string remove_error{};
  REQUIRE(remove_active_server_info(output_path.string(), &remove_error));
  REQUIRE(remove_error.empty());
  REQUIRE_FALSE(std::filesystem::exists(output_path));
  std::filesystem::remove_all(temp_dir);
}

TEST_CASE("Runtime server info read returns nullopt for missing file") {
  const std::filesystem::path missing_path =
      unique_temp_dir() / "this-file-does-not-exist.json";

  std::string error{};
  const auto loaded = read_active_server_info(missing_path.string(), &error);
  REQUIRE_FALSE(loaded.has_value());
  REQUIRE_FALSE(error.empty());
}

TEST_CASE("Runtime server info read rejects malformed payload") {
  const std::filesystem::path temp_dir = unique_temp_dir();
  const std::filesystem::path malformed_path = temp_dir / "malformed.json";
  std::filesystem::create_directories(temp_dir);

  {
    std::ofstream out(malformed_path, std::ios::trunc);
    REQUIRE(out.is_open());
    out << "{\"host\": \"127.0.0.1\", \"port\": \"not-a-number\"}\n";
  }

  std::string error{};
  const auto loaded = read_active_server_info(malformed_path.string(), &error);
  REQUIRE_FALSE(loaded.has_value());
  REQUIRE_FALSE(error.empty());

  std::filesystem::remove_all(temp_dir);
}

} // namespace
} // namespace devy::runtime
