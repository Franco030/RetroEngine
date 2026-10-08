#pragma once

#include "physics/Collision.hpp"

#include <raylib.h>

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

} // namespace retro