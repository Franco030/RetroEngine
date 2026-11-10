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
  // false: suena igual en los dos oidos y hacia donde mires; solo baja con la distancia
  // (musica de un cuarto, que te rodea en lugar de salir de un punto)
  bool directional = true;
};

} // namespace retro