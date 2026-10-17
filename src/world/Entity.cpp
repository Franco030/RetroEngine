#include "world/Entity.hpp"

#include "graphics/Billboard.hpp"

#include <algorithm>
#include <cmath>
#include <stdexcept>
#include <utility>

namespace retro {

Entity::Entity(std::string name, std::shared_ptr<ModelResource> model,
               std::shared_ptr<TextureResource> texture)
    : name_(std::move(name)), model_(std::move(model)), texture_(std::move(texture)) {
  if (!model_ || !model_->IsValid()) {
    throw std::invalid_argument("Entity '" + name_ + "': modelo nulo o invalido");
  }
}

Entity::Entity(std::string name, std::shared_ptr<TextureResource> sprite, Vector2 size)
    : name_(std::move(name)), texture_(std::move(sprite)), spriteSize_(size) {
  if (!texture_ || !texture_->IsValid()) {
    throw std::invalid_argument("Entity '" + name_ + "': sprite nulo o invalido");
  }
  if (size.x <= 0.0f || size.y <= 0.0f) {
    throw std::invalid_argument("Entity '" + name_ + "': 'size' debe ser positivo");
  }
}

void Entity::Update(float dt, Vector3 focus) {
  if (updateFn_)
    updateFn_(*this, dt);

  if (facePlayer) {
    const float dx = focus.x - transform.position.x;
    const float dz = focus.z - transform.position.z;
    const float dist2 = dx * dx + dz * dz;
    if (dist2 > 1e-4f && dist2 <= lookRange * lookRange) {
      const float target = std::atan2(dx, dz) * RAD2DEG;
      const float diff = std::remainder(target - transform.rotation.y, 360.0f);
      const float step = turnSpeed * dt;
      transform.rotation.y =
          std::remainder(transform.rotation.y + std::clamp(diff, -step, step), 360.0f);
    }
  }

  if (idle) {
    idleTime_ += dt;
    const float t = idleTime_ * idle->speed * 3.0f + idle->phase;
    drawOffset_ = {0.0f, std::fabs(std::sin(t)) * idle->bob, 0.0f};
    drawRoll_ = std::sin(t * 0.5f) * idle->sway;
  }
}

void Entity::Draw() {
  if (!visible || IsSprite())
    return;

  Model& model = model_->Get();

  // El modelo es compartido: se configura justo antes de cada dibujado.
  if (texture_) {
    for (int m = 0; m < model.materialCount; ++m) {
      SetMaterialTexture(&model.materials[m], MATERIAL_MAP_DIFFUSE, texture_->Get());
    }
  }

  Transform visual = transform;
  visual.position = Vector3Add(visual.position, drawOffset_);
  visual.rotation.z += drawRoll_;
  model.transform = visual.ToMatrix();
  DrawModel(model, {0.0f, 0.0f, 0.0f}, 1.0f, tint);
}

void Entity::DrawSprite(const Camera3D& camera, Vector3 shade) {
  if (!visible || !IsSprite())
    return;

  const Vector2 size{spriteSize_.x * std::fabs(transform.scale.x),
                     spriteSize_.y * std::fabs(transform.scale.y)};

  const Vector3 k = emissive ? Vector3{1.0f, 1.0f, 1.0f} : shade;
  auto channel = [](unsigned char t, float f) {
    return static_cast<unsigned char>(std::min(255.0f, static_cast<float>(t) * f));
  };
  const Color c{channel(tint.r, k.x), channel(tint.g, k.y), channel(tint.b, k.z), tint.a};

  DrawUprightBillboard(camera, texture_->Get(), transform.position, size, c);
}

} // namespace retro