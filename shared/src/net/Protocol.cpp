#include "shared/net/Protocol.h"

namespace devy::net {

std::string serialize(const Packet& packet) {
  nlohmann::json root;
  root["type"] = static_cast<uint8_t>(packet.type);
  root["payload"] = packet.payload;
  return root.dump();
}

Packet deserialize(const std::string& data) {
  Packet packet{MessageType::JoinRequest, nlohmann::json{}};
  auto root = nlohmann::json::parse(data, nullptr, false);
  if (root.is_discarded()) {
    return packet;
  }

  packet.type = static_cast<MessageType>(root.value("type", 1));
  packet.payload = root.value("payload", nlohmann::json{});
  return packet;
}

} // namespace devy::net
