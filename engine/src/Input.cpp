#include "engine/Input.h"

namespace devy::engine {

const Uint8* Input::keys_ = nullptr;

void Input::update() {
  keys_ = SDL_GetKeyboardState(nullptr);
}

bool Input::key_down(SDL_Scancode scancode) {
  if (!keys_) {
    keys_ = SDL_GetKeyboardState(nullptr);
  }
  return keys_[scancode] != 0;
}

} // namespace devy::engine
