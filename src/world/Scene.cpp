#include "world/Scene.hpp"

#include <algorithm>
#include <stdexcept>
#include <utility>

namespace retro {

Entity& Scene::Add(std::unique_ptr<Entity> entity) {
  if (!entity)
    throw std::invalid_argument("Scene::Add: entidad nula");

  entities_.push_back(std::move(entity));
  return *entities_.back();
}

Entity* Scene::Find(const std::string& name) {
  auto it = std::find_if(entities_.begin(), entities_.end(),
                         [&](const auto& e) { return e->Name() == name; });
  return it != entities_.end() ? it->get() : nullptr;
}

bool Scene::Remove(const std::string& name) {
  return std::erase_if(entities_, [&](const auto& e) { return e->Name() == name; });
}

void Scene::Update(float dt) {
  for (auto& e : entities_)
    e->Update(dt);
}

void Scene::Draw() {
  for (auto& e : entities_)
    e->Draw();
}

} // namespace retro