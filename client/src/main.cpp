#include "client/ChunkSyncApplier.h"
#include "client/PredictionReconciler.h"
#include "client/WeaponFireEmitter.h"
#include "engine/Application.h"
#include "engine/Camera.h"
#include "engine/Input.h"
#include "engine/Renderer.h"
#include "shared/Config.h"
#include "shared/Log.h"
#include "shared/game/Weapons.h"
#include "shared/net/Protocol.h"
#include "shared/voxel/World.h"

#include <SDL2/SDL.h>
#include <enet/enet.h>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <filesystem>
#include <limits>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

#include <glm/gtc/matrix_transform.hpp>

namespace {

using devy::log::Level;

struct TreasureSpawnView {
  uint64_t spawn_id{0U};
  float x{0.0F};
  float y{0.0F};
};

std::string resolve_path(const std::string& path) {
  if (std::filesystem::exists(path)) {
    return path;
  }
  const auto alt = std::filesystem::path("..") / path;
  if (std::filesystem::exists(alt)) {
    return alt.string();
  }
  return path;
}

std::optional<uint32_t> json_to_u32(const nlohmann::json& value) {
  if (value.is_number_unsigned()) {
    const uint64_t raw = value.get<uint64_t>();
    if (raw <= static_cast<uint64_t>(std::numeric_limits<uint32_t>::max())) {
      return static_cast<uint32_t>(raw);
    }
    return std::nullopt;
  }
  if (value.is_number_integer()) {
    const int64_t raw = value.get<int64_t>();
    if (raw >= 0 && raw <= static_cast<int64_t>(std::numeric_limits<uint32_t>::max())) {
      return static_cast<uint32_t>(raw);
    }
  }
  return std::nullopt;
}

std::optional<uint64_t> json_to_u64(const nlohmann::json& value) {
  if (value.is_number_unsigned()) {
    return value.get<uint64_t>();
  }
  if (value.is_number_integer()) {
    const int64_t raw = value.get<int64_t>();
    if (raw >= 0) {
      return static_cast<uint64_t>(raw);
    }
  }
  return std::nullopt;
}

std::optional<float> json_to_float(const nlohmann::json& value) {
  if (!value.is_number()) {
    return std::nullopt;
  }
  const double raw = value.get<double>();
  if (raw < static_cast<double>(std::numeric_limits<float>::lowest()) ||
      raw > static_cast<double>(std::numeric_limits<float>::max())) {
    return std::nullopt;
  }
  return static_cast<float>(raw);
}

void send_packet(ENetPeer* peer, const devy::net::Packet& packet) {
  if (peer == nullptr) {
    return;
  }
  const std::string payload = devy::net::serialize(packet);
  ENetPacket* out = enet_packet_create(payload.data(), payload.size(), ENET_PACKET_FLAG_RELIABLE);
  enet_peer_send(peer, 0U, out);
}

void rebuild_chunk_meshes(devy::engine::Renderer* renderer, const devy::voxel::World& world) {
  if (renderer == nullptr) {
    return;
  }
  renderer->clear();
  for (const auto& entry : world.chunks()) {
    const auto& coord = entry.first;
    const auto& chunk = entry.second;
    const auto mesh = chunk.build_mesh();
    if (mesh.vertices.empty() || mesh.indices.empty()) {
      continue;
    }

    const glm::vec3 chunk_min(static_cast<float>(coord.x * devy::voxel::kChunkSize),
                              static_cast<float>(coord.y * devy::voxel::kChunkSize),
                              static_cast<float>(coord.z * devy::voxel::kChunkSize));
    const glm::vec3 chunk_max =
        chunk_min + glm::vec3(static_cast<float>(devy::voxel::kChunkSize));

    glm::mat4 model(1.0F);
    model = glm::translate(model, chunk_min);
    renderer->submit(mesh, model, chunk_min, chunk_max);
  }
}

std::vector<TreasureSpawnView> parse_treasure_spawns(const nlohmann::json& snapshot_payload) {
  std::vector<TreasureSpawnView> out{};
  if (!snapshot_payload.is_object() || !snapshot_payload.contains("treasure_spawns") ||
      !snapshot_payload["treasure_spawns"].is_array()) {
    return out;
  }

  for (const auto& spawn : snapshot_payload["treasure_spawns"]) {
    if (!spawn.is_object() || !spawn.contains("spawn_id") || !spawn.contains("position") ||
        !spawn["position"].is_object()) {
      continue;
    }
    const auto spawn_id = json_to_u64(spawn["spawn_id"]);
    const auto x = json_to_float(spawn["position"].value("x", nlohmann::json{}));
    const auto y = json_to_float(spawn["position"].value("y", nlohmann::json{}));
    if (!spawn_id.has_value() || !x.has_value() || !y.has_value()) {
      continue;
    }
    out.push_back({spawn_id.value(), x.value(), y.value()});
  }
  return out;
}

std::pair<float, float> input_move_axes(const devy::engine::Camera& camera) {
  glm::vec3 forward = camera.forward();
  forward.y = 0.0F;
  if (glm::length(forward) > 0.0F) {
    forward = glm::normalize(forward);
  }
  glm::vec3 right = camera.right_dir();
  right.y = 0.0F;
  if (glm::length(right) > 0.0F) {
    right = glm::normalize(right);
  }

  glm::vec2 move(0.0F, 0.0F);
  if (devy::engine::Input::key_down(SDL_SCANCODE_W)) {
    move.x += forward.x;
    move.y += forward.z;
  }
  if (devy::engine::Input::key_down(SDL_SCANCODE_S)) {
    move.x -= forward.x;
    move.y -= forward.z;
  }
  if (devy::engine::Input::key_down(SDL_SCANCODE_D)) {
    move.x += right.x;
    move.y += right.z;
  }
  if (devy::engine::Input::key_down(SDL_SCANCODE_A)) {
    move.x -= right.x;
    move.y -= right.z;
  }

  if (glm::length(move) > 1.0F) {
    move = glm::normalize(move);
  }
  return {move.x, move.y};
}

std::optional<uint64_t>
closest_spawn_in_range(const std::vector<TreasureSpawnView>& spawns, float x, float y,
                       float max_distance) {
  std::optional<uint64_t> best_spawn{};
  float best_distance_sq = max_distance * max_distance;
  for (const auto& spawn : spawns) {
    const float dx = spawn.x - x;
    const float dy = spawn.y - y;
    const float dist_sq = (dx * dx) + (dy * dy);
    if (dist_sq <= best_distance_sq) {
      best_distance_sq = dist_sq;
      best_spawn = spawn.spawn_id;
    }
  }
  return best_spawn;
}

void push_quit_event() {
  SDL_Event quit{};
  quit.type = SDL_QUIT;
  SDL_PushEvent(&quit);
}

} // namespace

int main(int argc, char** argv) {
  devy::engine::Application app{};
  devy::engine::AppConfig app_config{};
  app_config.title = "Devy FPS Client";
  if (!app.init(app_config)) {
    return 1;
  }

  std::string config_path = "config/server.json";
  if (argc > 1) {
    config_path = argv[1];
  }
  config_path = resolve_path(config_path);
  const nlohmann::json server_config = devy::config::load_json(config_path);

  int port = server_config.value("port", 7777);
  if (port <= 0 || port > static_cast<int>(std::numeric_limits<uint16_t>::max())) {
    port = 7777;
  }
  std::string host = server_config.value("host", std::string("127.0.0.1"));

  int world_height = 256;
  int draw_distance_chunks = 8;
  if (server_config.contains("map") && server_config["map"].is_object()) {
    world_height = server_config["map"].value("world_height", world_height);
    draw_distance_chunks =
        server_config["map"].value("draw_distance_chunks", draw_distance_chunks);
  }
  if (world_height <= 0) {
    world_height = 256;
  }
  if (draw_distance_chunks <= 0) {
    draw_distance_chunks = 8;
  }

  int heartbeat_timeout_ms = 10000;
  if (server_config.contains("session") && server_config["session"].is_object()) {
    heartbeat_timeout_ms =
        server_config["session"].value("heartbeat_timeout_ms", heartbeat_timeout_ms);
  }
  if (heartbeat_timeout_ms <= 0) {
    heartbeat_timeout_ms = 10000;
  }
  const uint64_t heartbeat_interval_ms = static_cast<uint64_t>(
      std::max(100, std::min(2000, heartbeat_timeout_ms / 2)));

  double movement_speed_units_per_second = 6.0;
  if (server_config.contains("runtime") && server_config["runtime"].is_object()) {
    movement_speed_units_per_second =
        server_config["runtime"].value("movement_speed_units_per_second",
                                       movement_speed_units_per_second);
  }
  if (!std::isfinite(movement_speed_units_per_second) ||
      movement_speed_units_per_second <= 0.0) {
    movement_speed_units_per_second = 6.0;
  }

  auto weapons = devy::game::load_weapons(resolve_path("config/weapons.json"));
  std::string active_weapon_id = "rifle";
  if (!weapons.empty() && !weapons.front().id.empty()) {
    active_weapon_id = weapons.front().id;
  }

  devy::engine::Renderer renderer{};
  if (!renderer.init(resolve_path("shaders/voxel.vert"), resolve_path("shaders/voxel.frag"))) {
    devy::log::write(Level::Error, "Failed to initialize renderer.");
    app.shutdown();
    return 1;
  }

  devy::voxel::World world{};
  devy::engine::Camera camera{};
  SDL_SetRelativeMouseMode(SDL_TRUE);

  if (enet_initialize() != 0) {
    devy::log::write(Level::Error, "ENet initialization failed.");
    app.shutdown();
    return 1;
  }

  ENetHost* net_host = enet_host_create(nullptr, 1U, 2U, 0U, 0U);
  if (net_host == nullptr) {
    devy::log::write(Level::Error, "Failed to create ENet client host.");
    enet_deinitialize();
    app.shutdown();
    return 1;
  }

  ENetAddress address{};
  address.port = static_cast<enet_uint16>(port);
  if (enet_address_set_host(&address, host.c_str()) != 0) {
    devy::log::write(Level::Error, "Unable to resolve host: " + host + ".");
    enet_host_destroy(net_host);
    enet_deinitialize();
    app.shutdown();
    return 1;
  }

  ENetPeer* server_peer = enet_host_connect(net_host, &address, 2U, 0U);
  if (server_peer == nullptr) {
    devy::log::write(Level::Error, "Failed to queue ENet connection.");
    enet_host_destroy(net_host);
    enet_deinitialize();
    app.shutdown();
    return 1;
  }

  devy::client::PredictionReconciler reconciler(
      {static_cast<float>(movement_speed_units_per_second), 256U});
  devy::client::WeaponFireEmitter weapon_fire_emitter{};
  std::unordered_map<devy::voxel::ChunkCoord, uint32_t, devy::voxel::ChunkCoordHash>
      chunk_revisions{};
  std::vector<TreasureSpawnView> known_spawns{};

  uint32_t local_player_id = 0U;
  uint32_t next_pickup_seq = 1U;
  uint64_t next_heartbeat_at_ms = 0U;
  bool connection_ready = false;
  bool joined = false;
  bool fire_button_was_down = false;
  bool pickup_button_was_down = false;
  std::string last_match_state = "unknown";
  int last_match_seconds = -1;

  constexpr float fixed_dt_seconds = 1.0F / 60.0F;
  constexpr float pickup_max_distance_units = 3.0F;
  float input_accumulator = 0.0F;
  auto fixed_dt_ns = std::chrono::duration_cast<std::chrono::nanoseconds>(
      std::chrono::duration<float>(fixed_dt_seconds));

  devy::log::write(Level::Info, "Client started. Connecting to " + host + ":" +
                                    std::to_string(port) + ".");

  app.run([&](float dt) {
    devy::engine::Input::update();

    if (devy::engine::Input::key_down(SDL_SCANCODE_ESCAPE)) {
      push_quit_event();
      return;
    }

    int mouse_dx = 0;
    int mouse_dy = 0;
    SDL_GetRelativeMouseState(&mouse_dx, &mouse_dy);
    camera.rotate(static_cast<float>(mouse_dx), static_cast<float>(-mouse_dy));

    ENetEvent event{};
    while (enet_host_service(net_host, &event, 0) > 0) {
      if (event.type == ENET_EVENT_TYPE_CONNECT) {
        connection_ready = true;
        devy::log::write(Level::Info, "Connected to server transport; sending join request.");
        send_packet(event.peer, {devy::net::MessageType::JoinRequest,
                                 {{"player_name", "player_local"},
                                  {"client_build", "interactive-client"}},
                                 devy::net::kProtocolVersion});
      } else if (event.type == ENET_EVENT_TYPE_RECEIVE) {
        const std::string data(reinterpret_cast<char*>(event.packet->data), event.packet->dataLength);
        const auto parsed = devy::net::try_deserialize(data);
        if (!parsed.ok()) {
          devy::log::write(
              Level::Warn,
              "Dropped invalid packet from server: " +
                  std::string(devy::net::to_string(parsed.error)) + " (" + parsed.detail + ").");
          enet_packet_destroy(event.packet);
          continue;
        }

        if (parsed.packet.type == devy::net::MessageType::JoinAccept) {
          const bool accepted = parsed.packet.payload.value("accepted", false);
          if (!accepted) {
            const std::string reason = parsed.packet.payload.value("reason", "join_rejected");
            devy::log::write(Level::Error, "Join rejected by server: " + reason + ".");
            push_quit_event();
            enet_packet_destroy(event.packet);
            continue;
          }

          const auto player_id = json_to_u32(parsed.packet.payload.value("player_id", nlohmann::json{}));
          if (!player_id.has_value()) {
            devy::log::write(Level::Error, "Join accept missing valid player_id.");
            push_quit_event();
            enet_packet_destroy(event.packet);
            continue;
          }

          joined = true;
          local_player_id = player_id.value();
          next_pickup_seq = 1U;
          next_heartbeat_at_ms = SDL_GetTicks64();
          reconciler.reset();
          weapon_fire_emitter.reset();
          known_spawns.clear();
          chunk_revisions.clear();
          world.clear();
          renderer.clear();
          devy::log::write(Level::Info, "Join accepted player_id=" + std::to_string(local_player_id) + ".");
        } else if (parsed.packet.type == devy::net::MessageType::StateSnapshot) {
          if (!joined || local_player_id == 0U) {
            enet_packet_destroy(event.packet);
            continue;
          }

          if (const auto reconciliation =
                  reconciler.consume_snapshot(parsed.packet.payload, local_player_id);
              reconciliation.has_value() && reconciliation->applied &&
              reconciliation->correction_distance > 0.35F) {
            devy::log::write(
                Level::Info,
                "Reconciled input_seq=" + std::to_string(reconciliation->acked_input_seq) +
                    " correction=" + std::to_string(reconciliation->correction_distance) + ".");
          }

          if (const auto chunk_sync = devy::client::parse_chunk_sync(parsed.packet.payload);
              chunk_sync.has_value()) {
            const auto chunk_result = devy::client::apply_chunk_sync(
                &world, world_height, chunk_sync.value(), &chunk_revisions);
            if (chunk_result.changed) {
              rebuild_chunk_meshes(&renderer, world);
            }
          }

          known_spawns = parse_treasure_spawns(parsed.packet.payload);

          if (parsed.packet.payload.contains("events") && parsed.packet.payload["events"].is_array()) {
            for (const auto& event_payload : parsed.packet.payload["events"]) {
              if (!event_payload.is_object() || !event_payload.contains("type") ||
                  !event_payload["type"].is_string()) {
                continue;
              }
              const std::string event_type = event_payload["type"].get<std::string>();
              if ((event_type == "damage_event" || event_type == "death_event") &&
                  event_payload.value("victim_id", 0U) == local_player_id) {
                devy::log::write(Level::Info, "Snapshot event: " + event_type + ".");
              }
            }
          }

          if (parsed.packet.payload.contains("match_state") &&
              parsed.packet.payload["match_state"].is_object()) {
            const auto& match_state = parsed.packet.payload["match_state"];
            const std::string state = match_state.value("state", std::string("unknown"));
            const int remaining_seconds = static_cast<int>(std::floor(
                match_state.value("remaining_seconds", 0.0)));
            if (state != last_match_state || remaining_seconds != last_match_seconds) {
              last_match_state = state;
              last_match_seconds = remaining_seconds;
              devy::log::write(Level::Info,
                               "Match state: " + state + " (" +
                                   std::to_string(remaining_seconds) + "s).");
            }
          }
        } else if (parsed.packet.type == devy::net::MessageType::InventoryUpdate) {
          const auto player_id = json_to_u32(parsed.packet.payload.value("player_id", nlohmann::json{}));
          if (player_id.has_value() && player_id.value() == local_player_id) {
            const int coins = parsed.packet.payload.value("coins", 0);
            const int item_count = parsed.packet.payload.value("item_count", 0);
            devy::log::write(Level::Info, "Inventory update: coins=" + std::to_string(coins) +
                                              " items=" + std::to_string(item_count) + ".");
          }
        } else if (parsed.packet.type == devy::net::MessageType::DamageEvent) {
          const auto victim_id = json_to_u32(parsed.packet.payload.value("victim_id", nlohmann::json{}));
          if (victim_id.has_value() && victim_id.value() == local_player_id) {
            const int damage = parsed.packet.payload.value("damage", 0);
            const bool lethal = parsed.packet.payload.value("lethal", false);
            devy::log::write(Level::Info, "Damage received: damage=" + std::to_string(damage) +
                                              " lethal=" + std::string(lethal ? "true" : "false") +
                                              ".");
          }
        } else if (parsed.packet.type == devy::net::MessageType::MatchState) {
          const std::string state = parsed.packet.payload.value("state", std::string("unknown"));
          const int remaining_seconds = static_cast<int>(
              std::floor(parsed.packet.payload.value("remaining_seconds", 0.0)));
          if (state != last_match_state || remaining_seconds != last_match_seconds) {
            last_match_state = state;
            last_match_seconds = remaining_seconds;
            devy::log::write(Level::Info,
                             "Reliable match_state: " + state + " (" +
                                 std::to_string(remaining_seconds) + "s).");
          }
        }

        enet_packet_destroy(event.packet);
      } else if (event.type == ENET_EVENT_TYPE_DISCONNECT) {
        connection_ready = false;
        joined = false;
        local_player_id = 0U;
        devy::log::write(Level::Warn, "Disconnected from server.");
        push_quit_event();
      }
    }

    if (connection_ready && joined && local_player_id != 0U && server_peer != nullptr) {
      const uint64_t now_ms = SDL_GetTicks64();
      if (now_ms >= next_heartbeat_at_ms) {
        send_packet(server_peer,
                    {devy::net::MessageType::Heartbeat,
                     {{"player_id", local_player_id}, {"client_time_ms", now_ms}},
                     devy::net::kProtocolVersion});
        next_heartbeat_at_ms = now_ms + heartbeat_interval_ms;
      }
    }

    if (joined && local_player_id != 0U && server_peer != nullptr) {
      input_accumulator += dt;
      while (input_accumulator >= fixed_dt_seconds) {
        const auto [move_x, move_y] = input_move_axes(camera);
        const bool jump = devy::engine::Input::key_down(SDL_SCANCODE_SPACE);
        const uint32_t input_seq =
            reconciler.queue_local_input(move_x, move_y, jump, false, fixed_dt_ns);

        send_packet(server_peer,
                    {devy::net::MessageType::PlayerInput,
                     {{"player_id", local_player_id},
                      {"input_seq", input_seq},
                      {"move_x", move_x},
                      {"move_y", move_y},
                      {"jump", jump},
                      {"fire", false}},
                     devy::net::kProtocolVersion});
        input_accumulator -= fixed_dt_seconds;
      }
    }

    const Uint32 mouse_mask = SDL_GetMouseState(nullptr, nullptr);
    const bool fire_button_down = (mouse_mask & SDL_BUTTON(SDL_BUTTON_LEFT)) != 0U;
    if (fire_button_down && !fire_button_was_down && joined && local_player_id != 0U &&
        server_peer != nullptr) {
      const auto& predicted = reconciler.state();
      const glm::vec3 view = camera.forward();
      const auto fire_result =
          weapon_fire_emitter.emit(local_player_id, active_weapon_id, predicted.position_x,
                                   predicted.position_y, view.x, view.z, SDL_GetTicks64());
      if (fire_result.accepted()) {
        send_packet(server_peer, fire_result.packet);
      }
    }
    fire_button_was_down = fire_button_down;

    const bool pickup_button_down = devy::engine::Input::key_down(SDL_SCANCODE_E);
    if (pickup_button_down && !pickup_button_was_down && joined && local_player_id != 0U &&
        server_peer != nullptr) {
      const auto& predicted = reconciler.state();
      const auto spawn_id = closest_spawn_in_range(known_spawns, predicted.position_x,
                                                   predicted.position_y, pickup_max_distance_units);
      if (spawn_id.has_value()) {
        send_packet(server_peer,
                    {devy::net::MessageType::TreasurePickup,
                     {{"player_id", local_player_id},
                      {"pickup_seq", next_pickup_seq++},
                      {"spawn_id", spawn_id.value()}},
                     devy::net::kProtocolVersion});
      }
    }
    pickup_button_was_down = pickup_button_down;

    const auto& predicted = reconciler.state();
    const int world_x = static_cast<int>(std::lround(predicted.position_x));
    const int world_z = static_cast<int>(std::lround(predicted.position_y));
    const float eye_y =
        std::max(2.0F, static_cast<float>(world.height_at(world_x, world_z)) + 2.0F);
    camera.set_position(glm::vec3(predicted.position_x, eye_y, predicted.position_y));

    int width = 0;
    int height = 0;
    SDL_GetWindowSize(app.window(), &width, &height);
    const float aspect =
        (height > 0) ? static_cast<float>(width) / static_cast<float>(height) : 1.0F;
    const float max_distance = static_cast<float>(draw_distance_chunks * devy::voxel::kChunkSize);
    renderer.flush(camera.view_matrix(), camera.proj_matrix(aspect), camera.position(),
                   max_distance);

    enet_host_flush(net_host);
  });

  if (server_peer != nullptr) {
    enet_peer_disconnect(server_peer, 0U);
    const auto disconnect_deadline =
        std::chrono::steady_clock::now() + std::chrono::milliseconds(250);
    ENetEvent event{};
    while (std::chrono::steady_clock::now() < disconnect_deadline) {
      if (enet_host_service(net_host, &event, 5) <= 0) {
        continue;
      }
      if (event.type == ENET_EVENT_TYPE_RECEIVE && event.packet != nullptr) {
        enet_packet_destroy(event.packet);
      }
      if (event.type == ENET_EVENT_TYPE_DISCONNECT) {
        break;
      }
    }
  }
  enet_host_destroy(net_host);
  enet_deinitialize();

  app.shutdown();
  return 0;
}
