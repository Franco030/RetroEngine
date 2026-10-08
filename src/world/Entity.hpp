#pragma once

#include "assets/Resources.hpp"
#include "graphics/Transform.hpp"
#include "physics/Collision.hpp"
#include <world/Logic.hpp>

#include <raylib.h>

#include <functional>
#include <memory>
#include <optional>
#include <string>

namespace retro {

// Objeto del mundo: un Transform propio + un modelo (y textura) compartidos.
// Varias entidades pueden usar el mismo ModelResource con transformaciones distintas.
class Entity {
public:
  using UpdateFn = std::function<void(Entity&, float dt)>;

  // El shader retro debe estar ya asignado al modelo (ModelResource::SetShader).
  // Si se pasa textura, reemplaza el mapa difuso de TODOS los materiales al dibujar
  Entity(std::string name, std::shared_ptr<ModelResource> model,
         std::shared_ptr<TextureResource> texture = nullptr);

  Entity(const Entity&) = delete;
  Entity& operator=(const Entity&) = delete;

  const std::string& Name() const noexcept { return name_; }

  void SetUpdate(UpdateFn fn) { updateFn_ = std::move(fn); }
  void Update(float dt);
  void Draw();

  Transform transform;
  Color tint = WHITE;
  bool visible = true;
  std::optional<BoxCollider> collider;
  std::optional<Interaction> interact;

private:
  std::string name_;
  std::shared_ptr<ModelResource> model_;
  std::shared_ptr<TextureResource> texture_;
  UpdateFn updateFn_;
};

} // namespace retro