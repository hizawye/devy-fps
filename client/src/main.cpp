#include "client/WeaponFireEmitter.h"
#include "engine/Application.h"
#include "engine/Camera.h"
#include "engine/Input.h"
#include "engine/Physics.h"
#include "engine/PlayerController.h"
#include "engine/Renderer.h"
#include "shared/Config.h"
#include "shared/Log.h"
#include "shared/game/Match.h"
#include "shared/game/Treasure.h"
#include "shared/game/Weapons.h"
#include "shared/voxel/World.h"

#include <SDL2/SDL.h>

#include <algorithm>
#include <cmath>
#include <filesystem>
#include <string>

#include <glm/gtc/matrix_transform.hpp>

namespace {
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
  devy::engine::Application app;
  devy::engine::AppConfig config;
  config.title = "Devy FPS Client";

  if (!app.init(config)) {
    return 1;
  }

  std::string config_path = "config/server.json";
  if (argc > 1) {
    config_path = argv[1];
  }
  config_path = resolve_path(config_path);

  auto server_config = devy::config::load_json(config_path);
  int match_minutes = server_config.value("match_time_minutes", 60);
  int world_height = server_config["map"].value("world_height", 256);
  int chunks_x = server_config["map"].value("chunks_x", 64);
  int chunks_z = server_config["map"].value("chunks_z", 64);
  int draw_distance_chunks = server_config["map"].value("draw_distance_chunks", 8);

  devy::game::MatchTimer match_timer;
  match_timer.start(match_minutes * 60);

  auto weapons = devy::game::load_weapons(resolve_path("config/weapons.json"));
  auto treasures = devy::game::load_treasures(resolve_path("config/treasure.json"));
  devy::log::write(devy::log::Level::Info, "Loaded weapons: " + std::to_string(weapons.size()));
  devy::log::write(devy::log::Level::Info, "Loaded treasures: " + std::to_string(treasures.size()));
  std::string active_weapon_id = "rifle";
  if (!weapons.empty() && !weapons.front().id.empty()) {
    active_weapon_id = weapons.front().id;
  }
  constexpr uint32_t local_player_id = 1U;
  devy::client::WeaponFireEmitter weapon_fire_emitter{};
  bool fire_button_was_down = false;

  int preview_chunks = server_config["map"].value("preview_chunks", 8);
  int gen_chunks_x = std::min(chunks_x, preview_chunks);
  int gen_chunks_z = std::min(chunks_z, preview_chunks);
  if (gen_chunks_x != chunks_x || gen_chunks_z != chunks_z) {
    devy::log::write(devy::log::Level::Warn, "Streaming not implemented; generating preview chunks only.");
  }

  devy::voxel::World world;
  world.generate(gen_chunks_x, gen_chunks_z, world_height);

  devy::engine::Renderer renderer;
  std::string vert_path = resolve_path("shaders/voxel.vert");
  std::string frag_path = resolve_path("shaders/voxel.frag");
  if (!renderer.init(vert_path, frag_path)) {
    devy::log::write(devy::log::Level::Error, "Failed to initialize renderer.");
    return 1;
  }

  for (const auto& entry : world.chunks()) {
    const auto& coord = entry.first;
    const auto& chunk = entry.second;
    auto mesh = chunk.build_mesh();
    if (mesh.vertices.empty() || mesh.indices.empty()) {
      continue;
    }

    glm::vec3 chunk_min(
      static_cast<float>(coord.x * devy::voxel::kChunkSize),
      static_cast<float>(coord.y * devy::voxel::kChunkSize),
      static_cast<float>(coord.z * devy::voxel::kChunkSize)
    );
    glm::vec3 chunk_max = chunk_min + glm::vec3(static_cast<float>(devy::voxel::kChunkSize));

    glm::mat4 model(1.0f);
    model = glm::translate(model, chunk_min);
    renderer.submit(mesh, model, chunk_min, chunk_max);
  }

  devy::engine::PhysicsWorld physics;
  devy::engine::PlayerController player(physics.native(), 0.35f, 1.8f, 1.2f, 0.8f, 45.0f);

  int spawn_x = (gen_chunks_x * devy::voxel::kChunkSize) / 2;
  int spawn_z = (gen_chunks_z * devy::voxel::kChunkSize) / 2;
  float spawn_y = static_cast<float>(world.height_at(spawn_x, spawn_z)) + 2.0f;
  player.set_position(glm::vec3(static_cast<float>(spawn_x), spawn_y, static_cast<float>(spawn_z)));

  devy::engine::Camera camera;
  SDL_SetRelativeMouseMode(SDL_TRUE);

  devy::log::write(devy::log::Level::Info, "Client started. Press ESC to quit.");

  const float fixed_dt = 1.0f / 60.0f;
  float accumulator = 0.0f;

  app.run([&](float dt) {
    devy::engine::Input::update();
    match_timer.tick(dt);

    if (!match_timer.state().running) {
      devy::log::write(devy::log::Level::Info, "Match timer ended.");
      SDL_Event quit;
      quit.type = SDL_QUIT;
      SDL_PushEvent(&quit);
      return;
    }

    if (devy::engine::Input::key_down(SDL_SCANCODE_ESCAPE)) {
      SDL_Event quit;
      quit.type = SDL_QUIT;
      SDL_PushEvent(&quit);
      return;
    }

    int mouse_dx = 0;
    int mouse_dy = 0;
    SDL_GetRelativeMouseState(&mouse_dx, &mouse_dy);
    camera.rotate(static_cast<float>(mouse_dx), static_cast<float>(-mouse_dy));

    devy::engine::PlayerInput input;
    input.forward = devy::engine::Input::key_down(SDL_SCANCODE_W);
    input.back = devy::engine::Input::key_down(SDL_SCANCODE_S);
    input.right = devy::engine::Input::key_down(SDL_SCANCODE_D);
    input.left = devy::engine::Input::key_down(SDL_SCANCODE_A);
    input.sprint = devy::engine::Input::key_down(SDL_SCANCODE_LSHIFT);
    input.crouch = devy::engine::Input::key_down(SDL_SCANCODE_C);
    input.jump = devy::engine::Input::key_down(SDL_SCANCODE_SPACE);

    accumulator += dt;
    while (accumulator >= fixed_dt) {
      player.apply_input(fixed_dt, input, camera.forward(), camera.right_dir(), world);
      physics.step(fixed_dt);
      player.post_physics(world);
      accumulator -= fixed_dt;
    }

    glm::vec3 eye_pos = player.position();
    eye_pos.y += player.eye_height();
    camera.set_position(eye_pos);

    int mouse_x = 0;
    int mouse_y = 0;
    const Uint32 mouse_mask = SDL_GetMouseState(&mouse_x, &mouse_y);
    static_cast<void>(mouse_x);
    static_cast<void>(mouse_y);
    const bool fire_button_down = (mouse_mask & SDL_BUTTON(SDL_BUTTON_LEFT)) != 0U;
    if (fire_button_down && !fire_button_was_down) {
      const glm::vec3 view = camera.forward();
      const float planar_direction_x = view.x;
      const float planar_direction_y = view.z;
      const float planar_direction_length_sq =
          planar_direction_x * planar_direction_x + planar_direction_y * planar_direction_y;
      if (std::isfinite(planar_direction_length_sq) && planar_direction_length_sq > 0.000001F) {
        const auto fire_result = weapon_fire_emitter.emit(
            local_player_id, active_weapon_id, eye_pos.x, eye_pos.z, planar_direction_x,
            planar_direction_y, SDL_GetTicks64());
        if (fire_result.accepted()) {
          devy::log::write(devy::log::Level::Info,
                           "Queued weapon_fire shot_seq=" +
                               std::to_string(fire_result.shot_seq) + " weapon_id=" +
                               active_weapon_id + ".");
        } else {
          devy::log::write(
              devy::log::Level::Warn,
              "Rejected local fire request: " +
                  std::string(devy::client::to_string(fire_result.status)) + ".");
        }
      }
    }
    fire_button_was_down = fire_button_down;

    int width = 0;
    int height = 0;
    SDL_GetWindowSize(app.window(), &width, &height);
    float aspect = (height > 0) ? static_cast<float>(width) / static_cast<float>(height) : 1.0f;

    float max_distance = static_cast<float>(draw_distance_chunks * devy::voxel::kChunkSize);
    renderer.flush(camera.view_matrix(), camera.proj_matrix(aspect), camera.position(), max_distance);
  });

  app.shutdown();
  return 0;
}
