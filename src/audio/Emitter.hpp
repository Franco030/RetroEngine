#pragma once

#include <raylib.h>

#include <string>

namespace retro {

// Bucle de sonido fijado a un punto del mundo
struct Emitter {
  std::string key;
  std::string file;
  Vector3 position{0.0f, 0.0f, 0.0f};
  float volume = 1.0f;
  float radius = 10.0f;
  float minDistance = 1.0f;
};

} // namespace retro