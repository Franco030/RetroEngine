#pragma once

#include "physics/Collision.hpp"

#include <raylib.h>

#include <cmath>

namespace retro {

inline void DrawFloorGrid(int halfCells, float spacing, Color line, Color axis) {
  for (int i = -halfCells; i <= halfCells; ++i) {
    const Color c = (i == 0) ? axis : line;
    const float p = static_cast<float>(i) * spacing;

    for (int j = -halfCells; j < halfCells; ++j) {
      const float a = static_cast<float>(j) * spacing;
      const float b = static_cast<float>(j + 1) * spacing;
      DrawLine3D({p, 0.0f, a}, {p, 0.0f, b}, c);
      DrawLine3D({a, 0.0f, p}, {b, 0.0f, p}, c);
    }
  }
}

inline void DrawWorldBoxWires(const WorldBox& b, Color color) {
  Vector3 c[8];
  for (int i = 0; i < 8; ++i) {
    const float lx = ((i & 1) ? 1.0f : -1.0f) * b.halfX;
    const float lz = ((i & 2) ? 1.0f : -1.0f) * b.halfZ;
    c[i] = {b.center.x + lx * b.axisX.x + lz * b.axisZ.x, (i & 4) ? b.top : b.bottom,
            b.center.z + lx * b.axisX.y + lz * b.axisZ.y};
  }
  for (int i = 0; i < 8; ++i) {
    for (int bit : {1, 2, 4}) {
      const int j = i ^ bit;
      if (i < j)
        DrawLine3D(c[i], c[j], color);
    }
  }
}

inline void DrawPortalFrame(const WorldBox& b, Color color) {
  DrawWorldBoxWires(b, color);

  const float t = static_cast<float>(GetTime()) * 0.6f;
  for (int k = 0; k < 2; ++k) {
    const float f = std::fmod(t + 0.5f * static_cast<float>(k), 1.0f);
    const float y = b.bottom + (b.top - b.bottom) * f;

    Vector3 c[4];
    for (int i = 0; i < 4; ++i) {
      const float lx = (i == 1 || i == 2) ? b.halfX : -b.halfX;
      const float lz = (i >= 2) ? b.halfZ : -b.halfZ;
      c[i] = {b.center.x + lx * b.axisX.x + lz * b.axisZ.x, y,
              b.center.z + lx * b.axisX.y + lz * b.axisZ.y};
    }
    for (int i = 0; i < 4; ++i)
      DrawLine3D(c[i], c[(i + 1) % 4], color);
  }
}

} // namespace retro