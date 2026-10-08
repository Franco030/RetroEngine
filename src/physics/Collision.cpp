#include "physics/Collision.hpp"

#include <raymath.h>

#include <algorithm>
#include <cmath>

namespace retro {

namespace {

struct Probe {
  float lx, lz;
  float dx, dz;
  float dist2;
};

Probe MakeProbe(const WorldBox& b, Vector2 p) {
  const float rx = p.x - b.center.x;
  const float rz = p.y - b.center.z;

  Probe r{};
  r.lx = rx * b.axisX.x + rz * b.axisX.y;
  r.lz = rx * b.axisZ.x + rz * b.axisZ.y;
  r.dx = r.lx - std::clamp(r.lx, -b.halfX, b.halfX);
  r.dz = r.lz - std::clamp(r.lz, -b.halfZ, b.halfZ);
  r.dist2 = r.dx * r.dx + r.dz * r.dz;
  return r;
}

} // namespace

WorldBox MakeWorldBox(const BoxCollider& c, const Transform& t) {
  const Matrix m = t.ToMatrix();
  const Vector3 localMid = Vector3Scale(Vector3Add(c.min, c.max), 0.5f);
  const Vector3 localHalf = Vector3Scale(Vector3Subtract(c.max, c.min), 0.5f);

  WorldBox w;
  w.center = Vector3Transform(localMid, m);

  const Vector2 ax{m.m0, m.m2};
  const Vector2 az{m.m8, m.m10};
  const float lenX = Vector2Length(ax);
  const float lenZ = Vector2Length(az);

  w.axisX = lenX > 1e-6f ? Vector2Scale(ax, 1.0f / lenX) : Vector2{1.0f, 0.0f};
  w.axisZ = lenZ > 1e-6f ? Vector2Scale(az, 1.0f / lenZ) : Vector2{0.0f, 1.0f};
  w.halfX = localHalf.x * lenX;
  w.halfZ = localHalf.z * lenZ;

  const float y0 = t.position.y + c.min.y * t.scale.y;
  const float y1 = t.position.y + c.max.y * t.scale.y;
  w.bottom = std::min(y0, y1);
  w.top = std::max(y0, y1);
  return w;
}

bool OverlapsCircle(const WorldBox& box, Vector2 pos, float radius) {
  return MakeProbe(box, pos).dist2 < radius * radius;
}

bool PushOutCircle(const WorldBox& box, Vector2& pos, float radius) {
  const Probe pr = MakeProbe(box, pos);
  if (pr.dist2 >= radius * radius)
    return false;

  float nx, nz, push;
  if (pr.dist2 > 1e-8f) {
    // El centro está fuera de la caja: empujar a lo largo de la normal.
    const float dist = std::sqrt(pr.dist2);
    nx = pr.dx / dist;
    nz = pr.dz / dist;
    push = radius - dist;
  } else {
    // El centro está dentro: salir por la cara más cercana.
    const float px = box.halfX - std::fabs(pr.lx);
    const float pz = box.halfZ - std::fabs(pr.lz);
    if (px < pz) {
      nx = pr.lx >= 0.0f ? 1.0f : -1.0f;
      nz = 0.0f;
      push = px + radius;
    } else {
      nx = 0.0f;
      nz = pr.lz >= 0.0f ? 1.0f : -1.0f;
      push = pz + radius;
    }
  }

  // De coordenadas locales a mundo.
  pos.x += (nx * box.axisX.x + nz * box.axisZ.x) * push;
  pos.y += (nx * box.axisX.y + nz * box.axisZ.y) * push;
  return true;
}

} // namespace retro