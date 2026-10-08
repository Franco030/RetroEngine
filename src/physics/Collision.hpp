#pragma once

#include "graphics/Transform.hpp"

#include <raylib.h>

namespace retro {

// Caja en el espacio LOCAL de la entidad (antes de aplicar su Transform)
struct BoxCollider {
  Vector3 min{-0.5f, 0.0f, -0.5f};
  Vector3 max{0.5f, 1.0f, 0.5f};
};

// Caja ya en el mundo: orientada XZ (solo giro en y) y con rango vertical.
// Vector2 = (x, z) del mundo
struct WorldBox {
  Vector3 center{};
  Vector2 axisX{1.0f, 0.0f};
  Vector2 axisZ{0.0f, 1.0f};
  float halfX = 0.5f;
  float halfZ = 0.5f;
  float bottom = 0.0f;
  float top = 1.0f;
};

// Usa la misma matriz con la que se dibuja la entidad, asi colision y modelo
// siempre coinciden
WorldBox MakeWorldBox(const BoxCollider& collider, const Transform& transform);

// Circulo (jugador visto desde arriba) contra la caja, en el plano XZ
bool OverlapsCircle(const WorldBox& box, Vector2 pos, float radius);

// Si hay solape, mueve 'pos' fuera de la caja y devuelve true
bool PushOutCircle(const WorldBox& box, Vector2& pos, float radius);

} // namespace retro