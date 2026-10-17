#pragma once

#include "audio/Emitter.hpp"
#include "game/GameState.hpp"
#include "graphics/Light.hpp"
#include "physics/Collision.hpp"
#include "world/Entity.hpp"
#include "world/Logic.hpp"
#include "world/Trigger.hpp"

#include <raylib.h>

#include <cstddef>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <vector>

namespace retro {

struct SceneAudio {
  std::string music;
  float musicVolume = 0.6f;
  std::string ambient;
  float ambientVolume = 0.5f;
  std::vector<std::string> footsteps;
  float footstepVolume = 0.6f;
  float fade = 1.5f;
};

class Scene {
public:
  struct SpawnPoint {
    Vector3 position{0.0f, 0.0f, 9.0f};
    float yaw = 0.0f;
  };
  struct Bounds {
    Vector2 min{};
    Vector2 max{};
  };
  struct InteractHit {
    Entity* entity = nullptr;
    float distance = 0.0f;
  };

  SpawnPoint spawn;
  std::string path;
  std::vector<Trigger> triggers;
  SceneAudio audio;
  std::vector<PointLight> lights;
  std::vector<Emitter> emitters;
  std::optional<Bounds> bounds;

  Entity& Add(std::unique_ptr<Entity> entity);
  Entity* Find(const std::string& name);
  bool Remove(const std::string& name);

  void Clear() noexcept {
    entities_.clear();
    triggers.clear();
    lights.clear();
    emitters.clear();
    bounds.reset();
    triggersPrimed_ = false;
    audio = SceneAudio{};
  }

  void Update(float dt, Vector3 focus);
  void Draw(const Camera3D& camera, const Shader& spriteShader, float ambient,
            std::span<const PointLight> lights);

  std::size_t Count() const noexcept { return entities_.size(); }

  // Cajas de colisión en el mundo, con las transformaciones actuales.
  void CollectColliders(std::vector<WorldBox>& out) const;
  void CollectLights(std::vector<PointLight>& out) const;
  void CollectEmitters(std::vector<Emitter>& out) const;

  // Recalcula la visibilidad de las entidades con "showIf".
  // Una entidad oculta tampoco tiene colision ni se puede interactuar con ella
  void RefreshVisibility(const GameState& state);

  // Evalúa los triggers contra el cilindro del jugador y añade a 'fired' las
  // acciones que se disparan este frame. La primera llamada tras cargar solo
  // registra dónde está el jugador y reaplica el estado persistente.
  void UpdateTriggers(Vector3 feet, float radius, float height, GameState& state,
                      std::vector<Action>& fired);

  // Entidad interactuable más cercana a lo largo del rayo (dirección unitaria),
  // dentro de su rango, con condiciones cumplidas y sin nada sólido delante.
  InteractHit FindInteractable(Vector3 origin, Vector3 direction, const GameState& state);

  // Marca el "once" y añade las acciones de la interacción a 'fired'.
  void Interact(Entity& entity, GameState& state, std::vector<Action>& fired);

private:
  std::vector<std::unique_ptr<Entity>> entities_;
  bool triggersPrimed_ = false;
};

} // namespace retro