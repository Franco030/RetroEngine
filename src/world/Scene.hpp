#pragma once

#include "world/Entity.hpp"

#include <cstddef>
#include <memory>
#include <string>
#include <vector>

namespace retro {

class Scene {
public:
  Entity& Add(std::unique_ptr<Entity> entity);

  Entity* Find(const std::string& name);
  bool Remove(const std::string& name);

  void Update(float dt);
  void Draw();

  std::size_t Count() const noexcept { return entities_.size(); }

private:
  std::vector<std::unique_ptr<Entity>> entities_;
};

} // namespace retro