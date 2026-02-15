#include "server/SessionManager.h"

#include <algorithm>

namespace devy::server {
namespace {

constexpr std::size_t kMaxPlayerNameLength = 24;

} // namespace

SessionManager::SessionManager(SessionConfig config)
  : config_(config), slot_owners_(config.max_players + 1U, 0U) {}

JoinResult SessionManager::handle_join_request(std::uintptr_t peer_token,
                                               const std::string& player_name,
                                               SessionTimePoint now) {
  if (!is_valid_player_name(player_name)) {
    return JoinResult{JoinStatus::Rejected, JoinError::InvalidPlayerName, {}};
  }

  auto existing_session = sessions_by_peer_.find(peer_token);
  if (existing_session != sessions_by_peer_.end()) {
    if (existing_session->second.player_name != player_name) {
      return JoinResult{JoinStatus::Rejected, JoinError::NameInUse, {}};
    }
    existing_session->second.last_heartbeat = now;
    SessionView view{};
    view.player_id = existing_session->second.player_id;
    view.player_name = existing_session->second.player_name;
    view.peer_token = existing_session->second.peer_token;
    view.last_heartbeat = existing_session->second.last_heartbeat;
    return JoinResult{JoinStatus::AlreadyJoined, JoinError::None, view};
  }

  JoinStatus status = JoinStatus::Accepted;
  std::optional<uint32_t> slot;

  auto reserved_slot = slot_by_player_name_.find(player_name);
  if (reserved_slot != slot_by_player_name_.end()) {
    if (!slot_is_free(reserved_slot->second)) {
      return JoinResult{JoinStatus::Rejected, JoinError::NameInUse, {}};
    }
    slot = reserved_slot->second;
    status = JoinStatus::Reconnected;
  } else {
    slot = first_free_slot();
    if (!slot.has_value()) {
      return JoinResult{JoinStatus::Rejected, JoinError::ServerFull, {}};
    }
  }

  SessionRecord session{};
  session.player_id = *slot;
  session.player_name = player_name;
  session.peer_token = peer_token;
  session.last_heartbeat = now;

  occupy_slot(*slot, peer_token);
  slot_by_player_name_[player_name] = *slot;
  sessions_by_peer_.emplace(peer_token, session);

  SessionView view{};
  view.player_id = session.player_id;
  view.player_name = session.player_name;
  view.peer_token = session.peer_token;
  view.last_heartbeat = session.last_heartbeat;
  return JoinResult{status, JoinError::None, view};
}

bool SessionManager::handle_heartbeat(std::uintptr_t peer_token, SessionTimePoint now) {
  auto it = sessions_by_peer_.find(peer_token);
  if (it == sessions_by_peer_.end()) {
    return false;
  }
  it->second.last_heartbeat = now;
  return true;
}

std::optional<SessionView> SessionManager::handle_disconnect(std::uintptr_t peer_token) {
  auto it = sessions_by_peer_.find(peer_token);
  if (it == sessions_by_peer_.end()) {
    return std::nullopt;
  }

  SessionView view{};
  view.player_id = it->second.player_id;
  view.player_name = it->second.player_name;
  view.peer_token = it->second.peer_token;
  view.last_heartbeat = it->second.last_heartbeat;
  release_slot(it->second.player_id);
  sessions_by_peer_.erase(it);
  return view;
}

std::vector<SessionView> SessionManager::collect_timed_out(SessionTimePoint now) {
  std::vector<std::uintptr_t> timed_out_peers{};
  timed_out_peers.reserve(sessions_by_peer_.size());
  for (const auto& [peer_token, session] : sessions_by_peer_) {
    if (now - session.last_heartbeat > config_.heartbeat_timeout) {
      timed_out_peers.push_back(peer_token);
    }
  }

  std::vector<SessionView> timed_out{};
  timed_out.reserve(timed_out_peers.size());
  for (std::uintptr_t peer_token : timed_out_peers) {
    auto removed = handle_disconnect(peer_token);
    if (removed.has_value()) {
      timed_out.push_back(*removed);
    }
  }
  return timed_out;
}

std::vector<SessionView> SessionManager::active_sessions() const {
  std::vector<SessionView> sessions{};
  sessions.reserve(sessions_by_peer_.size());
  for (const auto& [peer_token, session] : sessions_by_peer_) {
    SessionView view{};
    view.player_id = session.player_id;
    view.player_name = session.player_name;
    view.peer_token = peer_token;
    view.last_heartbeat = session.last_heartbeat;
    sessions.push_back(view);
  }
  std::sort(sessions.begin(), sessions.end(), [](const SessionView& lhs, const SessionView& rhs) {
    if (lhs.player_id != rhs.player_id) {
      return lhs.player_id < rhs.player_id;
    }
    return lhs.peer_token < rhs.peer_token;
  });
  return sessions;
}

std::optional<SessionView> SessionManager::session_for_peer(std::uintptr_t peer_token) const {
  auto it = sessions_by_peer_.find(peer_token);
  if (it == sessions_by_peer_.end()) {
    return std::nullopt;
  }
  SessionView view{};
  view.player_id = it->second.player_id;
  view.player_name = it->second.player_name;
  view.peer_token = it->second.peer_token;
  view.last_heartbeat = it->second.last_heartbeat;
  return view;
}

std::size_t SessionManager::active_count() const {
  return sessions_by_peer_.size();
}

std::size_t SessionManager::capacity() const {
  return config_.max_players;
}

bool SessionManager::is_valid_player_name(const std::string& player_name) const {
  if (player_name.empty() || player_name.size() > kMaxPlayerNameLength) {
    return false;
  }
  return std::all_of(player_name.begin(), player_name.end(), [](unsigned char c) {
    return c >= 32U && c <= 126U;
  });
}

bool SessionManager::slot_is_free(uint32_t slot) const {
  if (slot == 0U || static_cast<std::size_t>(slot) >= slot_owners_.size()) {
    return false;
  }
  return slot_owners_[slot] == 0U;
}

std::optional<uint32_t> SessionManager::first_free_slot() const {
  for (uint32_t slot = 1; slot < slot_owners_.size(); ++slot) {
    if (slot_owners_[slot] == 0U) {
      return slot;
    }
  }
  return std::nullopt;
}

void SessionManager::occupy_slot(uint32_t slot, std::uintptr_t peer_token) {
  slot_owners_[slot] = peer_token;
}

void SessionManager::release_slot(uint32_t slot) {
  if (slot == 0U || static_cast<std::size_t>(slot) >= slot_owners_.size()) {
    return;
  }
  slot_owners_[slot] = 0U;
}

const char* to_string(JoinStatus status) {
  switch (status) {
    case JoinStatus::Accepted: return "accepted";
    case JoinStatus::Reconnected: return "reconnected";
    case JoinStatus::AlreadyJoined: return "already_joined";
    case JoinStatus::Rejected: return "rejected";
    default: return "unknown_join_status";
  }
}

const char* to_string(JoinError error) {
  switch (error) {
    case JoinError::None: return "none";
    case JoinError::InvalidPlayerName: return "invalid_player_name";
    case JoinError::NameInUse: return "name_in_use";
    case JoinError::ServerFull: return "server_full";
    default: return "unknown_join_error";
  }
}

} // namespace devy::server
