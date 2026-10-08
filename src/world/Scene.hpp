#pragma once

#include "physics/Collision.hpp"
#include "world/Entity.hpp"

#include <raylib.h>

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

  void Clear() noexcept { entities_.clear(); }

  void Update(float dt);
  void Draw();

  std::size_t Count() const noexcept { return entities_.size(); }

  struct SpawnPoint {
    Vector3 position{0.0f, 0.0f, 9.0f};
    float yaw = 0.0f;
  };
  SpawnPoint spawn;

  void CollectColliders(std::vector<WorldBox>& out) const;

private:
  std::vector<std::unique_ptr<Entity>> entities_;
};

} // namespace retro