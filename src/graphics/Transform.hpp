#pragma once

#include <raylib.h>
#include <raymath.h>

namespace retro {

struct Transform {
  Vector3 position{0.0f, 0.0f, 0.0f};
  Vector3 rotation{0.0f, 0.0f, 0.0f};
  Vector3 scale{1.0f, 1.0f, 1.0f};

  Matrix ToMatrix() const {
    const Vector3 radians = {rotation.x * DEG2RAD, rotation.y * DEG2RAD, rotation.z * DEG2RAD};
    const Matrix s = MatrixScale(scale.x, scale.y, scale.z);
    const Matrix r = MatrixRotateXYZ(radians);
    const Matrix t = MatrixTranslate(position.x, position.y, position.z);
    return MatrixMultiply(MatrixMultiply(s, r), t);
  }
};

} // namespace retro