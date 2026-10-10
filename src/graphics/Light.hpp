#pragma once

#include <raylib.h>

namespace retro {

constexpr int kMaxPointLights = 8;

struct PointLight {
  Vector3 position{0.0f, 0.0f, 0.0f};
  Vector3 color{1.0f, 1.0f, 1.0f};
  float radius = 8.0f;
};

} // namespace retro