#include "shared/net/Protocol.h"
#include "shared/voxel/WorldGenerationProfile.h"

#include <enet/enet.h>

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <limits>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <unordered_map>
#include <utility>
#include <vector>

namespace {

struct LoadClientConfig {
  std::string host{"127.0.0.1"};
  uint16_t port{17777};
  std::size_t clients{8U};
  int duration_seconds{8};
  int input_rate_hz{20};
  int heartbeat_ms{250};
  int malformed_rate_hz{0};
  std::string malformed_family{"legacy"};
  int malformed_burst_size{1};
  int disconnect_interval_ms{0};
  int reconnect_delay_ms{150};
};

struct BotState {
  ENetPeer* peer{nullptr};
  std::string name{};
  bool connected{false};
  bool joined{false};
  bool pending_reconnect{false};
  uint32_t player_id{0U};
  uint32_t input_seq{0U};
  std::chrono::steady_clock::time_point next_input_at{};
  std::chrono::steady_clock::time_point next_heartbeat_at{};
  std::chrono::steady_clock::time_point next_malformed_at{};
  std::chrono::steady_clock::time_point next_disconnect_at{};
  std::chrono::steady_clock::time_point reconnect_at{};
};

std::uintptr_t peer_token(const ENetPeer* peer) { return reinterpret_cast<std::uintptr_t>(peer); }

enum class MalformedFamily {
  Legacy,
  Mixed,
  InvalidJson,
  UnsupportedVersion,
  UnknownType,
  Schema,
  Envelope,
};

constexpr std::string_view kMalformedPayloadLegacy =
    "{\"version\":\"invalid\",\"type\":3,\"payload\":{\"player_id\":1}}";
constexpr std::string_view kMalformedPayloadInvalidJson =
    "{\"version\":1,\"type\":1,\"payload\":";
constexpr std::string_view kMalformedPayloadUnsupportedVersion =
    "{\"version\":99,\"type\":1,\"payload\":{\"player_name\":\"load-test\"}}";
constexpr std::string_view kMalformedPayloadUnknownType =
    "{\"version\":1,\"type\":999,\"payload\":{}}";
constexpr std::string_view kMalformedPayloadSchema =
    "{\"version\":1,\"type\":1,\"payload\":{}}";
constexpr std::string_view kMalformedPayloadEnvelope =
    "[1,2,3]";

constexpr std::array<std::string_view, 6U> kMixedMalformedPayloads{
    kMalformedPayloadLegacy,      kMalformedPayloadInvalidJson,
    kMalformedPayloadUnsupportedVersion, kMalformedPayloadUnknownType,
    kMalformedPayloadSchema,      kMalformedPayloadEnvelope};

std::string to_string(const MalformedFamily family) {
  switch (family) {
    case MalformedFamily::Legacy: return "legacy";
    case MalformedFamily::Mixed: return "mixed";
    case MalformedFamily::InvalidJson: return "invalid_json";
    case MalformedFamily::UnsupportedVersion: return "unsupported_version";
    case MalformedFamily::UnknownType: return "unknown_type";
    case MalformedFamily::Schema: return "schema";
    case MalformedFamily::Envelope: return "envelope";
  }
  return "legacy";
}

std::optional<MalformedFamily> parse_malformed_family(const std::string_view raw) {
  if (raw == "legacy") {
    return MalformedFamily::Legacy;
  }
  if (raw == "mixed") {
    return MalformedFamily::Mixed;
  }
  if (raw == "invalid_json") {
    return MalformedFamily::InvalidJson;
  }
  if (raw == "unsupported_version") {
    return MalformedFamily::UnsupportedVersion;
  }
  if (raw == "unknown_type") {
    return MalformedFamily::UnknownType;
  }
  if (raw == "schema") {
    return MalformedFamily::Schema;
  }
  if (raw == "envelope") {
    return MalformedFamily::Envelope;
  }
  return std::nullopt;
}

std::string_view malformed_payload_for(const MalformedFamily family, const std::size_t emission) {
  if (family == MalformedFamily::Mixed) {
    return kMixedMalformedPayloads[emission % kMixedMalformedPayloads.size()];
  }

  switch (family) {
    case MalformedFamily::Legacy: return kMalformedPayloadLegacy;
    case MalformedFamily::InvalidJson: return kMalformedPayloadInvalidJson;
    case MalformedFamily::UnsupportedVersion: return kMalformedPayloadUnsupportedVersion;
    case MalformedFamily::UnknownType: return kMalformedPayloadUnknownType;
    case MalformedFamily::Schema: return kMalformedPayloadSchema;
    case MalformedFamily::Envelope: return kMalformedPayloadEnvelope;
    case MalformedFamily::Mixed: break;
  }
  return kMalformedPayloadLegacy;
}

void print_usage(const char* argv0) {
  std::cerr << "Usage: " << argv0
            << " [--host <ipv4-or-hostname>] [--port <port>] [--clients <count>]"
               " [--seconds <duration>] [--input-rate-hz <hz>] [--heartbeat-ms <ms>]"
               " [--malformed-rate-hz <hz>]"
               " [--malformed-family <legacy|mixed|invalid_json|unsupported_version|unknown_type|schema|envelope>]"
               " [--malformed-burst-size <count>]"
               " [--disconnect-interval-ms <ms>]"
               " [--reconnect-delay-ms <ms>]\n";
}

std::optional<uint64_t> parse_u64(std::string_view raw) {
  if (raw.empty()) {
    return std::nullopt;
  }
  try {
    std::size_t parsed = 0U;
    const unsigned long long value = std::stoull(std::string(raw), &parsed, 10);
    if (parsed != raw.size()) {
      return std::nullopt;
    }
    return static_cast<uint64_t>(value);
  } catch (const std::exception&) {
    return std::nullopt;
  }
}

bool parse_args(int argc, char** argv, LoadClientConfig* config) {
  for (int i = 1; i < argc; ++i) {
    const std::string arg = argv[i];
    const auto require_value = [&](const std::string& flag) -> const char* {
      if (i + 1 >= argc) {
        std::cerr << "Missing value for " << flag << "\n";
        return nullptr;
      }
      return argv[++i];
    };

    if (arg == "--host") {
      const char* value = require_value(arg);
      if (value == nullptr) {
        return false;
      }
      config->host = value;
      continue;
    }
    if (arg == "--port") {
      const char* value = require_value(arg);
      if (value == nullptr) {
        return false;
      }
      const auto parsed = parse_u64(value);
      if (!parsed.has_value() || parsed.value() == 0U ||
          parsed.value() > static_cast<uint64_t>(std::numeric_limits<uint16_t>::max())) {
        std::cerr << "Invalid --port value: " << value << "\n";
        return false;
      }
      config->port = static_cast<uint16_t>(parsed.value());
      continue;
    }
    if (arg == "--clients") {
      const char* value = require_value(arg);
      if (value == nullptr) {
        return false;
      }
      const auto parsed = parse_u64(value);
      if (!parsed.has_value() || parsed.value() == 0U) {
        std::cerr << "Invalid --clients value: " << value << "\n";
        return false;
      }
      config->clients = static_cast<std::size_t>(parsed.value());
      continue;
    }
    if (arg == "--seconds") {
      const char* value = require_value(arg);
      if (value == nullptr) {
        return false;
      }
      const auto parsed = parse_u64(value);
      if (!parsed.has_value() || parsed.value() == 0U ||
          parsed.value() > static_cast<uint64_t>(std::numeric_limits<int>::max())) {
        std::cerr << "Invalid --seconds value: " << value << "\n";
        return false;
      }
      config->duration_seconds = static_cast<int>(parsed.value());
      continue;
    }
    if (arg == "--input-rate-hz") {
      const char* value = require_value(arg);
      if (value == nullptr) {
        return false;
      }
      const auto parsed = parse_u64(value);
      if (!parsed.has_value() || parsed.value() == 0U ||
          parsed.value() > static_cast<uint64_t>(std::numeric_limits<int>::max())) {
        std::cerr << "Invalid --input-rate-hz value: " << value << "\n";
        return false;
      }
      config->input_rate_hz = static_cast<int>(parsed.value());
      continue;
    }
    if (arg == "--heartbeat-ms") {
      const char* value = require_value(arg);
      if (value == nullptr) {
        return false;
      }
      const auto parsed = parse_u64(value);
      if (!parsed.has_value() || parsed.value() == 0U ||
          parsed.value() > static_cast<uint64_t>(std::numeric_limits<int>::max())) {
        std::cerr << "Invalid --heartbeat-ms value: " << value << "\n";
        return false;
      }
      config->heartbeat_ms = static_cast<int>(parsed.value());
      continue;
    }
    if (arg == "--malformed-rate-hz") {
      const char* value = require_value(arg);
      if (value == nullptr) {
        return false;
      }
      const auto parsed = parse_u64(value);
      if (!parsed.has_value() || parsed.value() > static_cast<uint64_t>(std::numeric_limits<int>::max())) {
        std::cerr << "Invalid --malformed-rate-hz value: " << value << "\n";
        return false;
      }
      config->malformed_rate_hz = static_cast<int>(parsed.value());
      continue;
    }
    if (arg == "--malformed-family") {
      const char* value = require_value(arg);
      if (value == nullptr) {
        return false;
      }
      if (!parse_malformed_family(value).has_value()) {
        std::cerr << "Invalid --malformed-family value: " << value << "\n";
        return false;
      }
      config->malformed_family = value;
      continue;
    }
    if (arg == "--malformed-burst-size") {
      const char* value = require_value(arg);
      if (value == nullptr) {
        return false;
      }
      const auto parsed = parse_u64(value);
      if (!parsed.has_value() || parsed.value() == 0U ||
          parsed.value() > static_cast<uint64_t>(std::numeric_limits<int>::max())) {
        std::cerr << "Invalid --malformed-burst-size value: " << value << "\n";
        return false;
      }
      config->malformed_burst_size = static_cast<int>(parsed.value());
      continue;
    }
    if (arg == "--disconnect-interval-ms") {
      const char* value = require_value(arg);
      if (value == nullptr) {
        return false;
      }
      const auto parsed = parse_u64(value);
      if (!parsed.has_value() || parsed.value() > static_cast<uint64_t>(std::numeric_limits<int>::max())) {
        std::cerr << "Invalid --disconnect-interval-ms value: " << value << "\n";
        return false;
      }
      config->disconnect_interval_ms = static_cast<int>(parsed.value());
      continue;
    }
    if (arg == "--reconnect-delay-ms") {
      const char* value = require_value(arg);
      if (value == nullptr) {
        return false;
      }
      const auto parsed = parse_u64(value);
      if (!parsed.has_value() || parsed.value() == 0U ||
          parsed.value() > static_cast<uint64_t>(std::numeric_limits<int>::max())) {
        std::cerr << "Invalid --reconnect-delay-ms value: " << value << "\n";
        return false;
      }
      config->reconnect_delay_ms = static_cast<int>(parsed.value());
      continue;
    }
    if (arg == "--help" || arg == "-h") {
      print_usage(argv[0]);
      return false;
    }

    std::cerr << "Unknown argument: " << arg << "\n";
    return false;
  }

  return true;
}

void send_packet(ENetPeer* peer, const devy::net::Packet& packet) {
  const std::string payload = devy::net::serialize(packet);
  ENetPacket* out = enet_packet_create(payload.data(), payload.size(), ENET_PACKET_FLAG_RELIABLE);
  enet_peer_send(peer, 0U, out);
}

void send_raw_packet(ENetPeer* peer, std::string_view payload) {
  ENetPacket* out =
      enet_packet_create(payload.data(), payload.size(), ENET_PACKET_FLAG_RELIABLE);
  enet_peer_send(peer, 0U, out);
}

bool connect_bot(ENetHost* client_host, const ENetAddress& address, std::size_t bot_index,
                 std::vector<BotState>* bots,
                 std::unordered_map<std::uintptr_t, std::size_t>* bot_index_by_peer) {
  if (bots == nullptr || bot_index_by_peer == nullptr || bot_index >= bots->size()) {
    return false;
  }

  BotState& bot = (*bots)[bot_index];
  if (bot.peer != nullptr) {
    return true;
  }

  ENetPeer* peer = enet_host_connect(client_host, &address, 2U, 0U);
  if (peer == nullptr) {
    return false;
  }

  bot.peer = peer;
  bot.connected = false;
  bot.joined = false;
  bot_index_by_peer->emplace(peer_token(peer), bot_index);
  return true;
}

std::optional<uint32_t> parse_player_id_from_join_accept(const nlohmann::json& payload) {
  if (!payload.contains("player_id")) {
    return std::nullopt;
  }

  const auto& player_id_json = payload["player_id"];
  if (player_id_json.is_number_unsigned()) {
    const uint64_t raw = player_id_json.get<uint64_t>();
    if (raw <= static_cast<uint64_t>(std::numeric_limits<uint32_t>::max())) {
      return static_cast<uint32_t>(raw);
    }
  } else if (player_id_json.is_number_integer()) {
    const int64_t raw = player_id_json.get<int64_t>();
    if (raw >= 0 && raw <= static_cast<int64_t>(std::numeric_limits<uint32_t>::max())) {
      return static_cast<uint32_t>(raw);
    }
  }

  return std::nullopt;
}

} // namespace

int main(int argc, char** argv) {
  LoadClientConfig config{};
  if (!parse_args(argc, argv, &config)) {
    print_usage(argv[0]);
    return 1;
  }

  if (enet_initialize() != 0) {
    std::cerr << "ENet initialization failed\n";
    return 1;
  }

  ENetHost* client_host = enet_host_create(nullptr, config.clients, 2U, 0U, 0U);
  if (client_host == nullptr) {
    std::cerr << "Failed to create ENet host for load client\n";
    enet_deinitialize();
    return 1;
  }

  ENetAddress address{};
  address.port = static_cast<enet_uint16>(config.port);
  if (enet_address_set_host(&address, config.host.c_str()) != 0) {
    std::cerr << "Unable to resolve host: " << config.host << "\n";
    enet_host_destroy(client_host);
    enet_deinitialize();
    return 1;
  }

  std::vector<BotState> bots(config.clients);
  std::unordered_map<std::uintptr_t, std::size_t> bot_index_by_peer{};
  bot_index_by_peer.reserve(config.clients * 2U);

  std::size_t connect_attempts = 0U;
  for (std::size_t i = 0U; i < config.clients; ++i) {
    bots[i].name = "bot_" + std::to_string(i + 1U);
    if (!connect_bot(client_host, address, i, &bots, &bot_index_by_peer)) {
      std::cerr << "Failed to create outgoing peer for bot index=" << i << "\n";
      continue;
    }
    ++connect_attempts;
  }

  const auto started_at = std::chrono::steady_clock::now();
  const auto finish_at = started_at + std::chrono::seconds(config.duration_seconds);
  const auto input_interval =
      std::chrono::milliseconds(std::max(1, 1000 / std::max(1, config.input_rate_hz)));
  const auto heartbeat_interval = std::chrono::milliseconds(config.heartbeat_ms);
  const std::optional<std::chrono::milliseconds> malformed_interval =
      (config.malformed_rate_hz > 0)
          ? std::optional<std::chrono::milliseconds>(
                std::chrono::milliseconds(std::max(1, 1000 / config.malformed_rate_hz)))
          : std::nullopt;
  const std::optional<std::chrono::milliseconds> disconnect_interval =
      (config.disconnect_interval_ms > 0)
          ? std::optional<std::chrono::milliseconds>(
                std::chrono::milliseconds(config.disconnect_interval_ms))
          : std::nullopt;
  const auto reconnect_delay = std::chrono::milliseconds(config.reconnect_delay_ms);
  const auto malformed_family = parse_malformed_family(config.malformed_family);
  if (!malformed_family.has_value()) {
    std::cerr << "Invalid malformed family: " << config.malformed_family << "\n";
    enet_host_destroy(client_host);
    enet_deinitialize();
    return 1;
  }

  constexpr float kTau = 6.28318530718F;

  std::size_t connected_count = 0U;
  std::size_t joined_count = 0U;
  std::size_t reconnect_attempts = 0U;
  std::size_t forced_disconnects = 0U;
  std::size_t input_packets_sent = 0U;
  std::size_t heartbeat_packets_sent = 0U;
  std::size_t malformed_packets_sent = 0U;
  std::size_t malformed_bursts_sent = 0U;
  std::size_t join_accept_world_gen_valid = 0U;
  std::size_t join_accept_world_gen_missing = 0U;
  std::size_t join_accept_world_gen_invalid = 0U;

  while (std::chrono::steady_clock::now() < finish_at) {
    ENetEvent event{};
    while (enet_host_service(client_host, &event, 2) > 0) {
      const auto token = peer_token(event.peer);
      auto it = bot_index_by_peer.find(token);
      if (it == bot_index_by_peer.end()) {
        if (event.type == ENET_EVENT_TYPE_RECEIVE && event.packet != nullptr) {
          enet_packet_destroy(event.packet);
        }
        continue;
      }
      BotState& bot = bots[it->second];

      if (event.type == ENET_EVENT_TYPE_CONNECT) {
        bot.connected = true;
        ++connected_count;
        const devy::net::Packet join{devy::net::MessageType::JoinRequest,
                                     {{"player_name", bot.name}, {"client_build", "load-test"}},
                                     devy::net::kProtocolVersion};
        send_packet(bot.peer, join);
      } else if (event.type == ENET_EVENT_TYPE_RECEIVE) {
        std::string payload(reinterpret_cast<char*>(event.packet->data), event.packet->dataLength);
        const auto parsed = devy::net::try_deserialize(payload);
        if (parsed.ok() && parsed.packet.type == devy::net::MessageType::JoinAccept &&
            parsed.packet.payload.value("accepted", false)) {
          const std::optional<uint32_t> parsed_player_id =
              parse_player_id_from_join_accept(parsed.packet.payload);

          const bool has_world_gen = parsed.packet.payload.contains("world_gen");
          bool world_gen_valid = false;
          if (has_world_gen) {
            world_gen_valid = devy::voxel::world_generation_profile_from_json(
                                  parsed.packet.payload["world_gen"])
                                  .has_value();
          }

          if (!has_world_gen) {
            ++join_accept_world_gen_missing;
            std::cerr << "Join accept missing world_gen payload for bot=" << bot.name << "\n";
          } else if (!world_gen_valid) {
            ++join_accept_world_gen_invalid;
            std::cerr << "Join accept world_gen payload invalid for bot=" << bot.name << "\n";
          } else {
            ++join_accept_world_gen_valid;
          }

          if (parsed_player_id.has_value() && world_gen_valid && !bot.joined) {
            bot.player_id = parsed_player_id.value();
            bot.joined = true;
            bot.pending_reconnect = false;
            ++joined_count;
            const auto now = std::chrono::steady_clock::now();
            bot.next_input_at = now;
            bot.next_heartbeat_at = now;
            bot.next_malformed_at = now;
            bot.next_disconnect_at = now + disconnect_interval.value_or(std::chrono::milliseconds::max());
          } else if (!bot.joined && !world_gen_valid && bot.peer != nullptr) {
            enet_peer_disconnect(bot.peer, 0U);
          }
        }
        enet_packet_destroy(event.packet);
      } else if (event.type == ENET_EVENT_TYPE_DISCONNECT) {
        bot_index_by_peer.erase(token);
        if (bot.peer == event.peer) {
          bot.peer = nullptr;
        }
        bot.connected = false;
        bot.joined = false;
        bot.player_id = 0U;
        if (std::chrono::steady_clock::now() < finish_at) {
          bot.pending_reconnect = true;
          bot.reconnect_at = std::chrono::steady_clock::now() + reconnect_delay;
        }
      }
    }

    const auto now = std::chrono::steady_clock::now();
    const auto elapsed_ms = static_cast<uint64_t>(
        std::chrono::duration_cast<std::chrono::milliseconds>(now - started_at).count());
    for (std::size_t i = 0U; i < bots.size(); ++i) {
      BotState& bot = bots[i];

      if (bot.pending_reconnect && bot.peer == nullptr && now >= bot.reconnect_at) {
        if (connect_bot(client_host, address, i, &bots, &bot_index_by_peer)) {
          ++connect_attempts;
          ++reconnect_attempts;
          bot.pending_reconnect = false;
        } else {
          bot.reconnect_at = now + reconnect_delay;
        }
      }

      if (!bot.joined || bot.peer == nullptr) {
        continue;
      }

      if (disconnect_interval.has_value() && now >= bot.next_disconnect_at) {
        enet_peer_disconnect(bot.peer, 0U);
        ++forced_disconnects;
        bot.next_disconnect_at = now + disconnect_interval.value();
        continue;
      }

      if (now >= bot.next_heartbeat_at) {
        const devy::net::Packet heartbeat{
            devy::net::MessageType::Heartbeat,
            {{"player_id", bot.player_id}, {"client_time_ms", elapsed_ms}},
            devy::net::kProtocolVersion};
        send_packet(bot.peer, heartbeat);
        ++heartbeat_packets_sent;
        bot.next_heartbeat_at = now + heartbeat_interval;
      }

      if (now >= bot.next_input_at) {
        const float phase = kTau * static_cast<float>(bot.input_seq % 128U) / 128.0F;
        const float move_x = std::cos(phase);
        const float move_y = std::sin(phase);
        const devy::net::Packet input{devy::net::MessageType::PlayerInput,
                                      {{"player_id", bot.player_id},
                                       {"input_seq", bot.input_seq},
                                       {"move_x", move_x},
                                       {"move_y", move_y},
                                       {"jump", false},
                                       {"sprint", false},
                                       {"crouch", false},
                                       {"fire", false}},
                                      devy::net::kProtocolVersion};
        send_packet(bot.peer, input);
        ++input_packets_sent;
        ++bot.input_seq;
        bot.next_input_at = now + input_interval;
      }

      if (malformed_interval.has_value() && now >= bot.next_malformed_at) {
        for (int burst = 0; burst < config.malformed_burst_size; ++burst) {
          send_raw_packet(
              bot.peer, malformed_payload_for(malformed_family.value(), malformed_packets_sent));
          ++malformed_packets_sent;
        }
        ++malformed_bursts_sent;
        bot.next_malformed_at = now + malformed_interval.value();
      }
    }

    enet_host_flush(client_host);
  }

  const auto disconnect_deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(750);
  for (auto& bot : bots) {
    if (bot.peer != nullptr && bot.connected) {
      enet_peer_disconnect(bot.peer, 0U);
    }
  }

  while (std::chrono::steady_clock::now() < disconnect_deadline) {
    ENetEvent event{};
    if (enet_host_service(client_host, &event, 5) <= 0) {
      continue;
    }

    if (event.type == ENET_EVENT_TYPE_RECEIVE && event.packet != nullptr) {
      enet_packet_destroy(event.packet);
      continue;
    }

    if (event.type != ENET_EVENT_TYPE_DISCONNECT) {
      continue;
    }

    const auto token = peer_token(event.peer);
    auto it = bot_index_by_peer.find(token);
    if (it != bot_index_by_peer.end()) {
      bots[it->second].peer = nullptr;
      bots[it->second].connected = false;
      bots[it->second].joined = false;
      bot_index_by_peer.erase(it);
    }
  }

  enet_host_destroy(client_host);
  enet_deinitialize();

  std::cout << "load_client summary: host=" << config.host << ':' << config.port
            << " clients=" << config.clients << " seconds=" << config.duration_seconds
            << " connect_attempts=" << connect_attempts << " connected=" << connected_count
            << " joined=" << joined_count << " reconnect_attempts=" << reconnect_attempts
            << " forced_disconnects=" << forced_disconnects
            << " malformed_sent=" << malformed_packets_sent
            << " malformed_family=" << to_string(malformed_family.value())
            << " malformed_burst_size=" << config.malformed_burst_size
            << " malformed_bursts=" << malformed_bursts_sent
            << " join_accept_world_gen_valid=" << join_accept_world_gen_valid
            << " join_accept_world_gen_missing=" << join_accept_world_gen_missing
            << " join_accept_world_gen_invalid=" << join_accept_world_gen_invalid
            << " heartbeats_sent=" << heartbeat_packets_sent
            << " inputs_sent=" << input_packets_sent << '\n';

  const bool world_gen_contract_ok =
      join_accept_world_gen_missing == 0U && join_accept_world_gen_invalid == 0U;
  return (joined_count > 0U && world_gen_contract_ok) ? 0 : 1;
}
