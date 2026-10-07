#pragma once

#include <raylib.h>

namespace retro {

inline void DrawFloorGrid(int halfCells, float spacing, Color line, Color axis) {
  const float extent = static_cast<float>(halfCells) * spacing;
  for (int i = -halfCells; i <= halfCells; ++i) {
    const Color c = (i == 0) ? axis : line;
    const float p = static_cast<float>(i) * spacing;

    DrawLine3D({p, 0.0f, -extent}, {p, 0.0f, extent}, c);
    DrawLine3D({-extent, 0.0f, p}, {extent, 0.0f, p}, c);
  }
}

} // namespace retro