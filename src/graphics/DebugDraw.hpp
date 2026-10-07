#pragma once

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

} // namespace retro