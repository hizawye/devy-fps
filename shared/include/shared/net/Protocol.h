#pragma once

#include <cstdint>
#include <nlohmann/json.hpp>
#include <string>

namespace devy::net {

enum class MessageType : uint8_t {
  JoinRequest = 1,
  JoinAccept = 2,
  PlayerInput = 3,
  StateSnapshot = 4,
  BlockUpdate = 5,
  InventoryUpdate = 6,
  DamageEvent = 7,
  MatchState = 8
};

struct Packet {
  MessageType type;
  nlohmann::json payload;
};

std::string serialize(const Packet& packet);
Packet deserialize(const std::string& data);

} // namespace devy::net
