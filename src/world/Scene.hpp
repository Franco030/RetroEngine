#pragma once

#include "game/GameState.hpp"
#include "physics/Collision.hpp"
#include "world/Entity.hpp"
#include "world/Trigger.hpp"

#include <raylib.h>

#include <cstddef>
#include <memory>
#include <string>
#include <vector>

namespace retro {

class Scene {
public:
  struct SpawnPoint {
    Vector3 position{0.0f, 0.0f, 9.0f};
    float yaw = 0.0f;
  };
  SpawnPoint spawn;
  std::string path;
  std::vector<Trigger> triggers;

  Entity& Add(std::unique_ptr<Entity> entity);
  Entity* Find(const std::string& name);
  bool Remove(const std::string& name);

  void Clear() noexcept {
    entities_.clear();
    triggers.clear();
    triggersPrimed_ = false;
  }

  void Update(float dt);
  void Draw();

  std::size_t Count() const noexcept { return entities_.size(); }

  // Cajas de colisión en el mundo, con las transformaciones actuales.
  void CollectColliders(std::vector<WorldBox>& out) const;

  // Evalúa los triggers contra el cilindro del jugador y añade a 'fired' las
  // acciones que se disparan este frame. Se ejecutan fuera (ActionRunner).
  // La primera llamada solo registra dónde está el jugador: aparecer dentro
  // de una zona no la dispara.
  void UpdateTriggers(Vector3 feet, float radius, float height, GameState& state,
                      std::vector<Action>& fired);

private:
  std::vector<std::unique_ptr<Entity>> entities_;
  bool triggersPrimed_ = false;
};

} // namespace retro