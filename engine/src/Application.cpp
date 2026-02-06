#include "engine/Application.h"

#include "shared/Log.h"

#include <glad/glad.h>

#include <string>

namespace devy::engine {

Application::Application() = default;

Application::~Application() {
  shutdown();
}

bool Application::init(const AppConfig& config) {
  if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_AUDIO | SDL_INIT_TIMER) != 0) {
    devy::log::write(devy::log::Level::Error, std::string("SDL init failed: ") + SDL_GetError());
    return false;
  }

  SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 4);
  SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 5);
  SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_CORE);
  SDL_GL_SetAttribute(SDL_GL_DOUBLEBUFFER, 1);
  SDL_GL_SetAttribute(SDL_GL_DEPTH_SIZE, 24);

  window_ = SDL_CreateWindow(
    config.title.c_str(),
    SDL_WINDOWPOS_CENTERED,
    SDL_WINDOWPOS_CENTERED,
    config.width,
    config.height,
    SDL_WINDOW_OPENGL | SDL_WINDOW_RESIZABLE
  );

  if (!window_) {
    devy::log::write(devy::log::Level::Error, std::string("Window creation failed: ") + SDL_GetError());
    return false;
  }

  gl_context_ = SDL_GL_CreateContext(window_);
  if (!gl_context_) {
    devy::log::write(devy::log::Level::Error, std::string("GL context failed: ") + SDL_GetError());
    return false;
  }

  if (!gladLoadGLLoader(reinterpret_cast<GLADloadproc>(SDL_GL_GetProcAddress))) {
    devy::log::write(devy::log::Level::Error, "Failed to initialize GLAD.");
    return false;
  }

  SDL_GL_SetSwapInterval(config.vsync ? 1 : 0);

  glEnable(GL_DEPTH_TEST);
  glEnable(GL_CULL_FACE);
  glCullFace(GL_BACK);
  glFrontFace(GL_CCW);

  running_ = true;
  return true;
}

void Application::run(const std::function<void(float)>& tick) {
  Uint64 last_counter = SDL_GetPerformanceCounter();

  while (running_) {
    SDL_Event event;
    while (SDL_PollEvent(&event)) {
      if (event.type == SDL_QUIT) {
        running_ = false;
      }
    }

    int drawable_w = 0;
    int drawable_h = 0;
    SDL_GL_GetDrawableSize(window_, &drawable_w, &drawable_h);
    if (drawable_w > 0 && drawable_h > 0) {
      glViewport(0, 0, drawable_w, drawable_h);
    }

    Uint64 current_counter = SDL_GetPerformanceCounter();
    float delta = static_cast<float>(current_counter - last_counter) / static_cast<float>(SDL_GetPerformanceFrequency());
    last_counter = current_counter;

    glClearColor(0.08f, 0.08f, 0.1f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

    if (tick) {
      tick(delta);
    }

    SDL_GL_SwapWindow(window_);
  }
}

void Application::shutdown() {
  if (gl_context_) {
    SDL_GL_DeleteContext(gl_context_);
    gl_context_ = nullptr;
  }

  if (window_) {
    SDL_DestroyWindow(window_);
    window_ = nullptr;
  }

  if (SDL_WasInit(SDL_INIT_VIDEO | SDL_INIT_AUDIO | SDL_INIT_TIMER)) {
    SDL_Quit();
  }

  running_ = false;
}

SDL_Window* Application::window() const {
  return window_;
}

} // namespace devy::engine
