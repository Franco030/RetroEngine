#include "graphics/Billboard.hpp"

#include <raymath.h>
#include <rlgl.h>

namespace retro {

void DrawUprightBillboard(const Camera3D& camera, const Texture2D& texture, Vector3 feet,
                          Vector2 size, Color tint) {
  Vector3 fwd = Vector3Subtract(camera.target, camera.position);
  fwd.y = 0.0f;
  const float len = Vector3Length(fwd);
  if (len < 1e-4f)
    return;
  fwd = Vector3Scale(fwd, 1.0f / len);

  const Vector3 right = {-fwd.z, 0.0f, fwd.x};
  const Vector3 half = Vector3Scale(right, size.x * 0.5f);
  const Vector3 up = {0.0f, size.y, 0.0f};

  const Vector3 bl = Vector3Subtract(feet, half);
  const Vector3 br = Vector3Add(feet, half);
  const Vector3 tr = Vector3Add(br, up);
  const Vector3 tl = Vector3Add(bl, up);

  if (rlCheckRenderBatchLimit(4))
    rlDrawRenderBatchActive();

  rlSetTexture(texture.id);
  rlBegin(RL_QUADS);
  rlColor4ub(tint.r, tint.g, tint.b, tint.a);
  rlTexCoord2f(0.0f, 1.0f);
  rlVertex3f(bl.x, bl.y, bl.z);
  rlTexCoord2f(1.0f, 1.0f);
  rlVertex3f(br.x, br.y, br.z);
  rlTexCoord2f(1.0f, 0.0f);
  rlVertex3f(tr.x, tr.y, tr.z);
  rlTexCoord2f(0.0f, 0.0f);
  rlVertex3f(tl.x, tl.y, tl.z);
  rlEnd();
  rlSetTexture(0);
}

} // namespace retro