#pragma once

#include "assets/Resources.hpp"
#include "graphics/Light.hpp"
#include "graphics/Transform.hpp"
#include "physics/Collision.hpp"
#include "world/Logic.hpp"

#include <raylib.h>

#include <cmath>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace retro {

struct EntityLight {
  Vector3 offset{0.0f, 0.0f, 0.0f};
  PointLight light;
};

struct EntitySound {
  Vector3 offset{0.0f, 0.0f, 0.0f};
  std::string file;
  float volume = 1.0f;
  float radius = 10.0f;
  float minDistance = 1.0f;
};

// Objeto del mundo: un Transform propio y, según el tipo, un modelo 3D
// compartido o un sprite billboard (textura + tamaño en el mundo).
class Entity {
public:
  using UpdateFn = std::function<void(Entity&, float dt)>;

  // Modelo. El shader retro debe estar ya asignado (ModelResource::SetShader).
  // Si se pasa textura, reemplaza el mapa difuso de los materiales sin textura propia
  // (ModelResource::OpenMaterials) al dibujar.
  Entity(std::string name, std::shared_ptr<ModelResource> model,
         std::shared_ptr<TextureResource> texture = nullptr);

  // Sprite billboard. 'size' = ancho y alto en unidades de mundo, antes de
  // transform.scale. transform.position es el punto bajo los pies (base centrada).
  Entity(std::string name, std::shared_ptr<TextureResource> sprite, Vector2 size);

  Entity(const Entity&) = delete;
  Entity& operator=(const Entity&) = delete;

  const std::string& Name() const noexcept { return name_; }
  bool IsSprite() const noexcept { return model_ == nullptr; }
  float SpriteHeight() const noexcept { return spriteSize_.y * std::fabs(transform.scale.y); }

  // Sprite animado: la imagen es una tira horizontal de 'frames' fotogramas iguales que se
  // pasan a 'fps' por segundo. 'phase' (segundos) desfasa unas copias de otras.
  void SetAnimation(int frames, float fps, float phase) noexcept {
    frames_ = frames;
    fps_ = fps;
    animTime_ = phase;
  }

  void SetUpdate(UpdateFn fn) { updateFn_ = std::move(fn); }
  void Update(float dt, Vector3 focus);

  // 'fullbright': shader sin iluminacion (el de los sprites) para los modelos emisivos.
  void Draw(const Shader& fullbright);
  void DrawSprite(const Camera3D& camera, Vector3 shade);

  BoundingBox LocalBounds();

  Transform transform;
  Color tint = WHITE;
  bool visible = true;
  bool emissive = false;
  // Capa de dibujo para superficies apoyadas en otras (modelos): 1 gana siempre a lo que tiene
  // debajo, -1 pierde. Ver depthBias en retro.vs.
  int layer = 0;
  bool facePlayer = false;
  float lookRange = 10.0f;
  float turnSpeed = 240.0f;
  std::vector<Condition> showIf;
  // Cajas de colision (vacia = se atraviesa). Varias si el modelo trae las suyas.
  std::vector<BoxCollider> colliders;
  std::optional<Interaction> interact;
  std::optional<EntityLight> light;
  std::optional<EntitySound> sound;

  struct Idle {
    float bob = 0.015f;
    float sway = 1.2f;
    float speed = 2.0f;
    float phase = 0.0f;
  };
  std::optional<Idle> idle;

private:
  std::string name_;
  float idleTime_ = 0.0f;
  int frames_ = 1;
  float fps_ = 8.0f;
  float animTime_ = 0.0f;
  Vector3 drawOffset_{0.0f, 0.0f, 0.0f};
  float drawRoll_ = 0.0f;
  std::shared_ptr<ModelResource> model_;
  std::shared_ptr<TextureResource> texture_;
  Vector2 spriteSize_{1.0f, 1.0f};
  UpdateFn updateFn_;
};

} // namespace retro