#pragma once

#include "audio/AudioSystem.hpp"

#include <raylib.h>

#include <cmath>

namespace retro {

class FootstepTracker {
public:
  void Reset(Vector3 feet) {
    last_ = feet;
    accumulated_ = 0.0f;
  }

  void Update(Vector3 feet, bool grounded, bool running, bool active, AudioSystem& audio) {
    const float moved = std::hypot(feet.x - last_.x, feet.z - last_.z);
    last_ = feet;
    if (!active || !grounded || moved > 1.0f)
      return;

    accumulated_ += moved;
    const float stride = running ? 2.6f : 1.9f;
    if (accumulated_ >= stride) {
      accumulated_ -= stride;
      audio.Footstep();
    }
  }

private:
  Vector3 last_{0.0f, 0.0f, 0.0f};
  float accumulated_ = 0.0f;
};

} // namespace retro