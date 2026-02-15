#pragma once

#include <cstddef>
#include <cstdint>
#include <nlohmann/json.hpp>
#include <string>

namespace devy::net {

inline constexpr uint32_t kProtocolVersion = 1;
inline constexpr uint32_t kMinSupportedProtocolVersion = 1;
inline constexpr uint32_t kMaxSupportedProtocolVersion = 1;

enum class MessageType : uint8_t {
  Invalid = 0,
  JoinRequest = 1,
  JoinAccept = 2,
  PlayerInput = 3,
  StateSnapshot = 4,
  BlockUpdate = 5,
  InventoryUpdate = 6,
  DamageEvent = 7,
  MatchState = 8,
  Heartbeat = 9,
  WeaponFire = 10,
  TreasurePickup = 11
};

struct Packet {
  MessageType type{MessageType::Invalid};
  nlohmann::json payload{};
  uint32_t version{kProtocolVersion};
};

enum class ProtocolError : uint8_t {
  None = 0,
  InvalidJson,
  MissingEnvelopeField,
  InvalidEnvelopeFieldType,
  UnsupportedVersion,
  UnknownMessageType,
  InvalidPayloadType,
  MissingPayloadField,
  InvalidPayloadFieldType
};

enum class PayloadFieldType : uint8_t {
  Number,
  Integer,
  Boolean,
  String,
  Object,
  Array
};

struct FieldRule {
  const char* name;
  PayloadFieldType type;
  bool required;
};

struct MessageSchema {
  MessageType type;
  const char* name;
  const FieldRule* fields;
  std::size_t field_count;
};

struct ParseResult {
  Packet packet{};
  ProtocolError error{ProtocolError::None};
  std::string detail{};

  [[nodiscard]] bool ok() const {
    return error == ProtocolError::None;
  }
};

std::string serialize(const Packet& packet);
Packet deserialize(const std::string& data);
ParseResult try_deserialize(const std::string& data);

const MessageSchema* schema_for(MessageType type);
bool is_supported_version(uint32_t version);
const char* to_string(MessageType type);
const char* to_string(ProtocolError error);

} // namespace devy::net
