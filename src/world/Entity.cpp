#include "world/Entity.hpp"

#include "graphics/Billboard.hpp"

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

void Entity::Update(float dt) {
  if (updateFn_)
    updateFn_(*this, dt);
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

  model.transform = transform.ToMatrix();
  DrawModel(model, {0.0f, 0.0f, 0.0f}, 1.0f, tint);
}

void Entity::DrawSprite(const Camera3D& camera, float shade) {
  if (!visible || !IsSprite())
    return;

  const Vector2 size{spriteSize_.x * std::fabs(transform.scale.x),
                     spriteSize_.y * std::fabs(transform.scale.y)};

  const float k = emissive ? 1.0f : shade;
  const Color c{static_cast<unsigned char>(tint.r * k), static_cast<unsigned char>(tint.g * k),
                static_cast<unsigned char>(tint.b * k), tint.a};

  DrawUprightBillboard(camera, texture_->Get(), transform.position, size, c);
}

} // namespace retro