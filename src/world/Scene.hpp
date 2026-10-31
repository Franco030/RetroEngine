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

  // El horizonte lejano (montanas, bosque): un modelo que se dibuja centrado en la camara, asi
  // nunca se llega a el ni corta la escena, con una bruma fija en vez de la niebla por
  // distancia. Opcionalmente un sol (sprite) detras, que el modelo tapa por profundidad.
  struct Panorama {
    std::shared_ptr<ModelResource> model;
    Color tint = WHITE;
    float haze = 0.4f;    // mezcla con el color de la niebla: 0 = nitido, 1 = invisible
    float y = -6.0f;      // altura del modelo (por debajo de 0 para que no se vea la base)
    std::shared_ptr<TextureResource> sun;
    Vector3 sunDirection{0.0f, 0.1f, -1.0f}; // unitaria, desde la camara
    float sunSize = 40.0f;
  };

  SpawnPoint spawn;
  std::string path;
  std::optional<Panorama> panorama;
  std::vector<Trigger> triggers;
  SceneAudio audio;
  std::vector<PointLight> lights;
  std::vector<Emitter> emitters;
  std::optional<Bounds> bounds;

  Entity& Add(std::unique_ptr<Entity> entity);
  Entity* Find(const std::string& name);
  // behind_player: la deja a la espalda de quien esta en 'feet' mirando a 'yawDegrees', mirandole.
  void PlaceBehind(Entity& entity, Vector3 feet, float yawDegrees, float distance, float side) const;
  bool Remove(const std::string& name);

  void Clear() noexcept {
    entities_.clear();
    triggers.clear();
    lights.clear();
    emitters.clear();
    bounds.reset();
    panorama.reset();
    triggersPrimed_ = false;
    audio = SceneAudio{};
  }

  void Update(float dt, Vector3 focus);
  // Coloca las entidades pegadas a otra ("attach") segun donde esta esa otra. Update lo hace solo;
  // el editor lo llama tambien cuando no anima, para dibujarlas en su sitio.
  void ApplyAttachments();
  // 'ambient' y 'sun' son los de la luz direccional: los sprites no tienen normal y reciben
  // el ambiente mas la mitad del sol.
  void Draw(const Camera3D& camera, const Shader& spriteShader, float ambient, float sun,
            std::span<const PointLight> lights);

  std::size_t Count() const noexcept { return entities_.size(); }

private:
  void DrawPanorama(const Camera3D& camera, const Shader& spriteShader) const;

public:

  Entity& EntityAt(std::size_t index) { return *entities_.at(index); }

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
  // La entidad visible mas cercana cuyo nombre empieza por 'prefix' que corta el rayo (por su
  // caja de dibujo), si no la tapa otra cosa: el tiro al blanco.
  Entity* FindTarget(Vector3 origin, Vector3 dir, const std::string& prefix);
  InteractHit FindInteractable(Vector3 origin, Vector3 direction, const GameState& state);

  // Marca el "once" y añade las acciones de la interacción a 'fired'.
  void Interact(Entity& entity, GameState& state, std::vector<Action>& fired);

  // Entidades con "seen": añade a 'fired' las acciones de las que se acaban de ver (o de dejar de
  // mirar) desde 'eye' mirando hacia 'forward' (unitaria).
  void UpdateSight(Vector3 eye, Vector3 forward, const GameState& state,
                   std::vector<Action>& fired);

private:
  // Si alguna entidad solida (que no sea 'self') corta el rayo antes de 'distance'.
  bool Occluded(Vector3 origin, Vector3 dir, float distance, const Entity* self) const;

  std::vector<std::unique_ptr<Entity>> entities_;
  bool triggersPrimed_ = false;
};

} // namespace retro