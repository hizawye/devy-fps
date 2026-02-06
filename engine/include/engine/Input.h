#pragma once

#include <SDL2/SDL.h>

namespace devy::engine {

class Input {
public:
  static void update();
  static bool key_down(SDL_Scancode scancode);

private:
  static const Uint8* keys_;
};

} // namespace devy::engine
