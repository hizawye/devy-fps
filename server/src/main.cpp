#include "shared/Config.h"
#include "shared/Log.h"
#include "shared/net/Protocol.h"

#include <enet/enet.h>

#include <atomic>
#include <csignal>
#include <filesystem>
#include <string>
#include <thread>

namespace {
std::atomic<bool> running{true};

void handle_signal(int) {
  running = false;
}

std::string resolve_path(const std::string& path) {
  if (std::filesystem::exists(path)) {
    return path;
  }
  auto alt = std::filesystem::path("..") / path;
  if (std::filesystem::exists(alt)) {
    return alt.string();
  }
  return path;
}
}

int main(int argc, char** argv) {
  std::signal(SIGINT, handle_signal);
  std::signal(SIGTERM, handle_signal);

  std::string config_path = "config/server.json";
  if (argc > 1) {
    config_path = argv[1];
  }
  config_path = resolve_path(config_path);

  auto config = devy::config::load_json(config_path);
  int max_players = config.value("max_players", 64);
  int port = config.value("port", 7777);

  if (enet_initialize() != 0) {
    devy::log::write(devy::log::Level::Error, "ENet initialization failed.");
    return 1;
  }

  ENetAddress address{};
  address.host = ENET_HOST_ANY;
  address.port = static_cast<enet_uint16>(port);

  ENetHost* server = enet_host_create(&address, max_players, 2, 0, 0);
  if (!server) {
    devy::log::write(devy::log::Level::Error, "Failed to create ENet server.");
    enet_deinitialize();
    return 1;
  }

  devy::log::write(devy::log::Level::Info, "Server started on port " + std::to_string(port) + ".");

  while (running) {
    ENetEvent event;
    while (enet_host_service(server, &event, 16) > 0) {
      switch (event.type) {
        case ENET_EVENT_TYPE_CONNECT: {
          devy::log::write(devy::log::Level::Info, "Client connected.");
          devy::net::Packet packet{devy::net::MessageType::JoinAccept, {
            {"message", "Welcome to Devy FPS"},
            {"max_players", max_players}
          }};
          std::string payload = devy::net::serialize(packet);
          ENetPacket* out = enet_packet_create(payload.data(), payload.size(), ENET_PACKET_FLAG_RELIABLE);
          enet_peer_send(event.peer, 0, out);
          break;
        }
        case ENET_EVENT_TYPE_RECEIVE: {
          std::string data(reinterpret_cast<char*>(event.packet->data), event.packet->dataLength);
          auto packet = devy::net::deserialize(data);
          if (packet.type == devy::net::MessageType::JoinRequest) {
            devy::log::write(devy::log::Level::Info, "Join request received.");
          }
          enet_packet_destroy(event.packet);
          break;
        }
        case ENET_EVENT_TYPE_DISCONNECT: {
          devy::log::write(devy::log::Level::Info, "Client disconnected.");
          break;
        }
        default:
          break;
      }
    }

    std::this_thread::sleep_for(std::chrono::milliseconds(5));
  }

  enet_host_destroy(server);
  enet_deinitialize();
  devy::log::write(devy::log::Level::Info, "Server stopped.");
  return 0;
}
