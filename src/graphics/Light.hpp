#pragma once

#include <raylib.h>

#include <cmath>
#include <cstdint>

namespace retro {

constexpr int kMaxPointLights = 8;

struct PointLight {
  Vector3 position{0.0f, 0.0f, 0.0f};
  Vector3 color{1.0f, 1.0f, 1.0f};
  float radius = 8.0f;
  float flicker = 0.0f;
  float flickerRate = 10.0f;
  float phase = 0.0f;
};

// Factor 1 - flicker * ruido(paso), con el paso cambiando 'flickerRate' veces por segundo.
inline float FlickerFactor(const PointLight& l, double time) {
  if (l.flicker <= 0.0f)
    return 1.0f;

  const auto step =
      static_cast<std::uint32_t>(static_cast<std::int64_t>(std::floor(time * l.flickerRate)) +
                                 static_cast<std::int64_t>(l.phase * 7919.0f));

  std::uint32_t x = step * 2654435761u;
  x ^= x >> 15;
  x *= 2246822519u;
  x ^= x >> 13;

  const float noise = static_cast<float>(x & 0xFFFFu) / 65535.0f;
  return 1.0f - l.flicker * noise;
}

} // namespace retro