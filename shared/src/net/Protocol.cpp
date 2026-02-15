#include "shared/net/Protocol.h"

#include <array>
#include <cstdint>
#include <limits>
#include <string>
#include <utility>

namespace devy::net {

namespace {

constexpr FieldRule kJoinRequestFields[] = {
  {"player_name", PayloadFieldType::String, true},
  {"client_build", PayloadFieldType::String, false},
  {"feature_flags", PayloadFieldType::Array, false}
};

constexpr FieldRule kJoinAcceptFields[] = {
  {"message", PayloadFieldType::String, true},
  {"max_players", PayloadFieldType::Integer, true},
  {"protocol_version", PayloadFieldType::Integer, true},
  {"feature_flags", PayloadFieldType::Array, false}
};

constexpr FieldRule kPlayerInputFields[] = {
  {"player_id", PayloadFieldType::Integer, true},
  {"input_seq", PayloadFieldType::Integer, true},
  {"move_x", PayloadFieldType::Number, true},
  {"move_y", PayloadFieldType::Number, true},
  {"jump", PayloadFieldType::Boolean, true},
  {"fire", PayloadFieldType::Boolean, true}
};

constexpr FieldRule kStateSnapshotFields[] = {
  {"tick", PayloadFieldType::Integer, true},
  {"players", PayloadFieldType::Array, true},
  {"events", PayloadFieldType::Array, false}
};

constexpr FieldRule kBlockUpdateFields[] = {
  {"player_id", PayloadFieldType::Integer, false},
  {"x", PayloadFieldType::Integer, true},
  {"y", PayloadFieldType::Integer, true},
  {"z", PayloadFieldType::Integer, true},
  {"block_id", PayloadFieldType::Integer, true}
};

constexpr FieldRule kInventoryUpdateFields[] = {
  {"player_id", PayloadFieldType::Integer, true},
  {"coins", PayloadFieldType::Integer, true}
};

constexpr FieldRule kDamageEventFields[] = {
  {"attacker_id", PayloadFieldType::Integer, true},
  {"victim_id", PayloadFieldType::Integer, true},
  {"damage", PayloadFieldType::Integer, true},
  {"lethal", PayloadFieldType::Boolean, true}
};

constexpr FieldRule kMatchStateFields[] = {
  {"state", PayloadFieldType::String, true},
  {"remaining_seconds", PayloadFieldType::Number, true},
  {"scoreboard", PayloadFieldType::Array, false}
};

constexpr FieldRule kHeartbeatFields[] = {
  {"player_id", PayloadFieldType::Integer, true},
  {"client_time_ms", PayloadFieldType::Integer, false}
};

constexpr FieldRule kWeaponFireFields[] = {
  {"player_id", PayloadFieldType::Integer, true},
  {"shot_seq", PayloadFieldType::Integer, true},
  {"weapon_id", PayloadFieldType::String, true},
  {"origin", PayloadFieldType::Object, true},
  {"direction", PayloadFieldType::Object, true},
  {"client_fire_time_ms", PayloadFieldType::Integer, false}
};

constexpr FieldRule kTreasurePickupFields[] = {
  {"player_id", PayloadFieldType::Integer, true},
  {"pickup_seq", PayloadFieldType::Integer, true},
  {"spawn_id", PayloadFieldType::Integer, true}
};

constexpr std::array<MessageSchema, 11> kMessageSchemas{{
  {MessageType::JoinRequest, "join_request", kJoinRequestFields, std::size(kJoinRequestFields)},
  {MessageType::JoinAccept, "join_accept", kJoinAcceptFields, std::size(kJoinAcceptFields)},
  {MessageType::PlayerInput, "player_input", kPlayerInputFields, std::size(kPlayerInputFields)},
  {MessageType::StateSnapshot, "state_snapshot", kStateSnapshotFields, std::size(kStateSnapshotFields)},
  {MessageType::BlockUpdate, "block_update", kBlockUpdateFields, std::size(kBlockUpdateFields)},
  {MessageType::InventoryUpdate, "inventory_update", kInventoryUpdateFields, std::size(kInventoryUpdateFields)},
  {MessageType::DamageEvent, "damage_event", kDamageEventFields, std::size(kDamageEventFields)},
  {MessageType::MatchState, "match_state", kMatchStateFields, std::size(kMatchStateFields)},
  {MessageType::Heartbeat, "heartbeat", kHeartbeatFields, std::size(kHeartbeatFields)},
  {MessageType::WeaponFire, "weapon_fire", kWeaponFireFields, std::size(kWeaponFireFields)},
  {MessageType::TreasurePickup, "treasure_pickup", kTreasurePickupFields,
   std::size(kTreasurePickupFields)}
}};

bool matches_type(const nlohmann::json& value, PayloadFieldType type) {
  switch (type) {
    case PayloadFieldType::Number: return value.is_number();
    case PayloadFieldType::Integer: return value.is_number_integer() || value.is_number_unsigned();
    case PayloadFieldType::Boolean: return value.is_boolean();
    case PayloadFieldType::String: return value.is_string();
    case PayloadFieldType::Object: return value.is_object();
    case PayloadFieldType::Array: return value.is_array();
    default: return false;
  }
}

ParseResult make_error(ProtocolError error, const std::string& detail) {
  ParseResult result{};
  result.error = error;
  result.detail = detail;
  return result;
}

ParseResult validate_payload(const Packet& packet) {
  const MessageSchema* schema = schema_for(packet.type);
  if (!schema) {
    return make_error(ProtocolError::UnknownMessageType, "Unknown message type.");
  }

  if (!packet.payload.is_object()) {
    return make_error(ProtocolError::InvalidPayloadType, "Payload must be a JSON object.");
  }

  for (std::size_t i = 0; i < schema->field_count; ++i) {
    const FieldRule& field = schema->fields[i];
    if (!packet.payload.contains(field.name)) {
      if (field.required) {
        return make_error(
          ProtocolError::MissingPayloadField,
          "Missing payload field `" + std::string(field.name) + "` for `" + std::string(schema->name) + "`.");
      }
      continue;
    }

    const auto& value = packet.payload[field.name];
    if (!matches_type(value, field.type)) {
      return make_error(
        ProtocolError::InvalidPayloadFieldType,
        "Invalid payload field type for `" + std::string(field.name) + "` in `" + std::string(schema->name) + "`.");
    }
  }

  return ParseResult{};
}

} // namespace

std::string serialize(const Packet& packet) {
  nlohmann::json root;
  root["version"] = packet.version;
  root["type"] = static_cast<uint8_t>(packet.type);
  root["payload"] = packet.payload;
  return root.dump();
}

Packet deserialize(const std::string& data) {
  const ParseResult result = try_deserialize(data);
  if (result.ok()) {
    return result.packet;
  }
  return Packet{};
}

ParseResult try_deserialize(const std::string& data) {
  const auto root = nlohmann::json::parse(data, nullptr, false);
  if (root.is_discarded()) {
    return make_error(ProtocolError::InvalidJson, "Failed to parse JSON packet.");
  }
  if (!root.is_object()) {
    return make_error(ProtocolError::InvalidEnvelopeFieldType, "Packet envelope must be a JSON object.");
  }

  uint32_t version = kProtocolVersion;
  if (root.contains("version")) {
    const auto& version_json = root["version"];
    if (!version_json.is_number_integer() && !version_json.is_number_unsigned()) {
      return make_error(ProtocolError::InvalidEnvelopeFieldType, "Envelope field `version` must be an integer.");
    }
    if (version_json.is_number_unsigned()) {
      const uint64_t version_value = version_json.get<uint64_t>();
      if (version_value == 0 || version_value > static_cast<uint64_t>(std::numeric_limits<uint32_t>::max())) {
        return make_error(ProtocolError::InvalidEnvelopeFieldType, "Envelope field `version` is out of range.");
      }
      version = static_cast<uint32_t>(version_value);
    } else {
      const int64_t version_value = version_json.get<int64_t>();
      if (version_value <= 0 || version_value > static_cast<int64_t>(std::numeric_limits<uint32_t>::max())) {
        return make_error(ProtocolError::InvalidEnvelopeFieldType, "Envelope field `version` is out of range.");
      }
      version = static_cast<uint32_t>(version_value);
    }
  }

  if (!is_supported_version(version)) {
    return make_error(ProtocolError::UnsupportedVersion, "Unsupported protocol version: " + std::to_string(version) + ".");
  }

  if (!root.contains("type")) {
    return make_error(ProtocolError::MissingEnvelopeField, "Envelope field `type` is required.");
  }

  const auto& type_json = root["type"];
  if (!type_json.is_number_integer() && !type_json.is_number_unsigned()) {
    return make_error(ProtocolError::InvalidEnvelopeFieldType, "Envelope field `type` must be an integer.");
  }

  uint8_t type_value = 0;
  if (type_json.is_number_unsigned()) {
    const uint64_t raw_type_value = type_json.get<uint64_t>();
    if (raw_type_value > static_cast<uint64_t>(std::numeric_limits<uint8_t>::max())) {
      return make_error(ProtocolError::UnknownMessageType, "Envelope field `type` is out of valid range.");
    }
    type_value = static_cast<uint8_t>(raw_type_value);
  } else {
    const int64_t raw_type_value = type_json.get<int64_t>();
    if (raw_type_value < 0 || raw_type_value > static_cast<int64_t>(std::numeric_limits<uint8_t>::max())) {
      return make_error(ProtocolError::UnknownMessageType, "Envelope field `type` is out of valid range.");
    }
    type_value = static_cast<uint8_t>(raw_type_value);
  }

  const MessageType type = static_cast<MessageType>(type_value);
  if (!schema_for(type)) {
    return make_error(
      ProtocolError::UnknownMessageType,
      "Unknown message type: " + std::to_string(static_cast<unsigned int>(type_value)) + ".");
  }

  if (!root.contains("payload")) {
    return make_error(ProtocolError::MissingEnvelopeField, "Envelope field `payload` is required.");
  }

  Packet packet{};
  packet.type = type;
  packet.payload = root["payload"];
  packet.version = version;

  ParseResult validation = validate_payload(packet);
  if (!validation.ok()) {
    return validation;
  }

  ParseResult result{};
  result.packet = std::move(packet);
  return result;
}

const MessageSchema* schema_for(MessageType type) {
  for (const auto& schema : kMessageSchemas) {
    if (schema.type == type) {
      return &schema;
    }
  }
  return nullptr;
}

bool is_supported_version(uint32_t version) {
  return version >= kMinSupportedProtocolVersion && version <= kMaxSupportedProtocolVersion;
}

const char* to_string(MessageType type) {
  const MessageSchema* schema = schema_for(type);
  if (schema) {
    return schema->name;
  }
  return "invalid";
}

const char* to_string(ProtocolError error) {
  switch (error) {
    case ProtocolError::None: return "none";
    case ProtocolError::InvalidJson: return "invalid_json";
    case ProtocolError::MissingEnvelopeField: return "missing_envelope_field";
    case ProtocolError::InvalidEnvelopeFieldType: return "invalid_envelope_field_type";
    case ProtocolError::UnsupportedVersion: return "unsupported_version";
    case ProtocolError::UnknownMessageType: return "unknown_message_type";
    case ProtocolError::InvalidPayloadType: return "invalid_payload_type";
    case ProtocolError::MissingPayloadField: return "missing_payload_field";
    case ProtocolError::InvalidPayloadFieldType: return "invalid_payload_field_type";
    default: return "unknown_error";
  }
}

} // namespace devy::net
