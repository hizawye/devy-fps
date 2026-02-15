#pragma once

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

namespace devy::server {

using SessionClock = std::chrono::steady_clock;
using SessionTimePoint = SessionClock::time_point;

struct SessionConfig {
  std::size_t max_players{64};
  std::chrono::milliseconds heartbeat_timeout{std::chrono::seconds(10)};
};

enum class JoinStatus : uint8_t {
  Accepted = 0,
  Reconnected,
  AlreadyJoined,
  Rejected
};

enum class JoinError : uint8_t {
  None = 0,
  InvalidPlayerName,
  NameInUse,
  ServerFull
};

struct SessionView {
  uint32_t player_id{0};
  std::string player_name{};
  std::uintptr_t peer_token{0};
  SessionTimePoint last_heartbeat{};
};

struct JoinResult {
  JoinStatus status{JoinStatus::Rejected};
  JoinError error{JoinError::None};
  SessionView session{};

  [[nodiscard]] bool accepted() const {
    return status == JoinStatus::Accepted || status == JoinStatus::Reconnected || status == JoinStatus::AlreadyJoined;
  }
};

class SessionManager {
 public:
  explicit SessionManager(SessionConfig config);

  JoinResult handle_join_request(std::uintptr_t peer_token, const std::string& player_name, SessionTimePoint now);
  bool handle_heartbeat(std::uintptr_t peer_token, SessionTimePoint now);
  std::optional<SessionView> handle_disconnect(std::uintptr_t peer_token);
  std::vector<SessionView> collect_timed_out(SessionTimePoint now);
  std::vector<SessionView> active_sessions() const;
  std::optional<SessionView> session_for_peer(std::uintptr_t peer_token) const;

  [[nodiscard]] std::size_t active_count() const;
  [[nodiscard]] std::size_t capacity() const;

 private:
  struct SessionRecord {
    uint32_t player_id{0};
    std::string player_name{};
    std::uintptr_t peer_token{0};
    SessionTimePoint last_heartbeat{};
  };

  [[nodiscard]] bool is_valid_player_name(const std::string& player_name) const;
  [[nodiscard]] bool slot_is_free(uint32_t slot) const;
  [[nodiscard]] std::optional<uint32_t> first_free_slot() const;
  void occupy_slot(uint32_t slot, std::uintptr_t peer_token);
  void release_slot(uint32_t slot);

  SessionConfig config_;
  std::vector<std::uintptr_t> slot_owners_;
  std::unordered_map<std::uintptr_t, SessionRecord> sessions_by_peer_;
  std::unordered_map<std::string, uint32_t> slot_by_player_name_;
};

const char* to_string(JoinStatus status);
const char* to_string(JoinError error);

} // namespace devy::server
