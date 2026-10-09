#pragma once

#include "assets/Resources.hpp"
#include "graphics/Transform.hpp"
#include "physics/Collision.hpp"
#include "world/Logic.hpp"

#include <raylib.h>

#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace retro {

// Objeto del mundo: un Transform propio y, según el tipo, un modelo 3D
// compartido o un sprite billboard (textura + tamaño en el mundo).
class Entity {
public:
  using UpdateFn = std::function<void(Entity&, float dt)>;

  // Modelo. El shader retro debe estar ya asignado (ModelResource::SetShader).
  // Si se pasa textura, reemplaza el mapa difuso de TODOS los materiales al dibujar.
  Entity(std::string name, std::shared_ptr<ModelResource> model,
         std::shared_ptr<TextureResource> texture = nullptr);

  // Sprite billboard. 'size' = ancho y alto en unidades de mundo, antes de
  // transform.scale. transform.position es el punto bajo los pies (base centrada).
  Entity(std::string name, std::shared_ptr<TextureResource> sprite, Vector2 size);

  Entity(const Entity&) = delete;
  Entity& operator=(const Entity&) = delete;

  const std::string& Name() const noexcept { return name_; }
  bool IsSprite() const noexcept { return model_ == nullptr; }

  void SetUpdate(UpdateFn fn) { updateFn_ = std::move(fn); }
  void Update(float dt);

  void Draw();
  void DrawSprite(const Camera3D& camera, float shade);

  Transform transform;
  Color tint = WHITE;
  bool visible = true;
  bool emissive = false;
  std::vector<Condition> showIf;
  std::optional<BoxCollider> collider;
  std::optional<Interaction> interact;

private:
  std::string name_;
  std::shared_ptr<ModelResource> model_;
  std::shared_ptr<TextureResource> texture_;
  Vector2 spriteSize_{1.0f, 1.0f};
  UpdateFn updateFn_;
};

} // namespace retro