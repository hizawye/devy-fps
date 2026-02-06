#pragma once

#include <SDL2/SDL.h>

#include <functional>
#include <string>

namespace devy::engine {

struct AppConfig {
  int width = 1280;
  int height = 720;
  std::string title = "Devy FPS";
  bool vsync = true;
};

class Application {
public:
  Application();
  ~Application();

  bool init(const AppConfig& config);
  void run(const std::function<void(float)>& tick);
  void shutdown();

  SDL_Window* window() const;

private:
  bool running_ = false;
  SDL_Window* window_ = nullptr;
  SDL_GLContext gl_context_ = nullptr;
};

} // namespace devy::engine
