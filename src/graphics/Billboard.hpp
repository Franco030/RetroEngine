#pragma once

#include <raylib.h>

namespace retro {

// Quad vertical con la base centrada en 'feet', orientado hacia la camara solo
// en el plano horizontal:
// el sprite nunca se inclina, aunque mires desde arriba
// usa el shader activo (BeginShaderMode) y debe lllamarse dentro de BeginMode3D
void DrawUprightBillboard(const Camera3D& camera, const Texture2D& texture, Vector3 feet,
                          Vector2 size, Color tint);

} // namespace retro