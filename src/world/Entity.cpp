#include "world/Entity.hpp"

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

void Entity::Update(float dt) {
  if (updateFn_)
    updateFn_(*this, dt);
}

void Entity::Draw() {
  if (!visible)
    return;

  Model& model = model_->Get();

  if (texture_) {
    for (int m = 0; m < model.materialCount; ++m) {
      SetMaterialTexture(&model.materials[m], MATERIAL_MAP_DIFFUSE, texture_->Get());
    }
  }

  model.transform = transform.ToMatrix();
  DrawModel(model, {0.0f, 0.0f, 0.0f}, 1.0f, tint);
}

} // namespace retro