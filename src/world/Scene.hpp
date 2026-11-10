#pragma once

#include "audio/Emitter.hpp"
#include "game/GameState.hpp"
#include "graphics/Light.hpp"
#include "physics/Collision.hpp"
#include "world/Entity.hpp"
#include "world/CylinderCity.hpp"
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

// A donde mira la camara cuando habla con 'e' o cuando un look_at/scare la nombra: a su caja de
// interaccion (o la del modelo), a la altura de la cara si es alta (40 cm bajo lo de arriba) o al
// medio si es baja. El juego lo usa para girar la vista; el editor, para dibujar el punto.
Vector3 LookPoint(Entity& e);

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
  // Cerrar los ojos (Q), solo en las escenas que traen "eyes". Con los ojos cerrados la bandera
  // "ojos_cerrados" vale 1 (lo que tenga showIf con ella aparece o desaparece). A los 'seconds'
  // seguidos gana el sueno: los ojos se abren solos y pasa 'onSleep'.
  struct Eyes {
    float seconds = 6.0f;
    std::vector<Action> onSleep;
  };
  std::optional<Eyes> eyes;

  // El tiempo (Puerto Niebla): lluvia 'rain' (0..1) que se ve caer y se oye ('sound', en bucle a
  // 'volume' por la lluvia), y tormenta: un relampago cada 'thunderMin'..'thunderMax' segundos (0 =
  // sin tormenta), con el trueno ('thunder', uno al azar) despues. 'indoor': bajo techo, la lluvia
  // solo se oye y los relampagos entran por las ventanas, mas flojos. Durante el destello, la
  // bandera "relampago" vale 1.
  struct Weather {
    float rain = 0.0f;
    bool indoor = false;
    std::string sound;
    float volume = 0.6f;
    float thunderMin = 0.0f;
    float thunderMax = 0.0f;
    std::vector<std::string> thunder;
    // Bancos de niebla: cada 'everyMin'..'everyMax' segundos la niebla entra (en 'fade' s), se
    // cierra hasta 'start'..'end' con el color 'color' y la neblina baja 'mist', se queda 'seconds'
    // y se va otra vez en 'fade'. Al entrar suena 'sound' (si hay).
    struct FogBank {
      float everyMin = 0.0f;
      float everyMax = 0.0f;
      float seconds = 15.0f;
      float fade = 3.0f;
      float start = 1.0f;
      float end = 10.0f;
      Color color{60, 64, 72, 255};
      float mist = 0.6f;
      std::string sound;
      float volume = 0.5f;
    };
    std::optional<FogBank> fogBank;
  };
  std::optional<Weather> weather;

  // El viento que empuja (el ojo de la tormenta): sopla desde 'center' hacia fuera (o hacia el,
  // con outward false) con 'strength' m/s, y cada 'gustMin'..'gustMax' s una racha de
  // 'gustSeconds' s que empuja con 'gustStrength' m/s mas (y suena 'gustSound'). Detras de algo
  // solido (a menos de 'shelter' m, en la direccion de donde viene) no empuja: a cubierto.
  struct Wind {
    Vector2 center{0.0f, 0.0f};
    bool outward = true;
    float strength = 1.0f;
    float gustMin = 0.0f;
    float gustMax = 0.0f;
    float gustSeconds = 3.0f;
    float gustStrength = 5.0f;
    std::string gustSound;
    float gustVolume = 0.8f;
    float shelter = 6.0f;
  };
  std::optional<Wind> wind;

  // Una luz que va con el jugador (el farolillo en la mano) mientras se cumpla 'if': en
  // 'offset' respecto a los ojos (x a la derecha, y arriba, z hacia delante).
  struct Lantern {
    std::vector<Condition> conditions;
    PointLight light;
    Vector3 offset{0.25f, -0.45f, 0.35f};
  };
  std::optional<Lantern> lantern;
  // Lo que el tiempo tapa del panorama ahora (un banco de niebla): main lo pone cada fotograma; el
  // panorama se dibuja con la mayor de las dos mezclas.
  float weatherHaze = 0.0f;

  // La camara de fotos (F): solo en las escenas que traen "photo" y mientras se cumpla 'if'. Cada
  // foto es un flash: la bandera "flash" vale 1 un instante (lo que tenga showIf o seen con ella
  // sale en la foto) y suma uno a "fotos". Entre una y otra, 'cooldown' segundos.
  struct Photo {
    std::vector<Condition> conditions;
    float cooldown = 1.5f;
  };
  std::optional<Photo> photo;

  // "city": la ciudad sin fin dentro de un cilindro (CylinderCity, la lleva el SceneManager). Sus
  // farolas y quioscos dan luz: 'streamedLights', que la ciudad rehace al poner y quitar trozos.
  std::optional<CityConfig> city;
  // El suelo de base del jugador ("floor": un numero, o false para ninguno). Ver Player::SetBaseFloor.
  std::optional<float> floor = 0.0f;
  std::vector<PointLight> streamedLights;

  // Mueve todo lo de la escena 'dz' en Z (la ciudad sin fin recentra el mundo para que los numeros
  // no crezcan sin fin): entidades (y lo que las mueve: drift, slide, arrive), triggers, luces,
  // sonidos, el inicio y los limites.
  void ShiftZ(float dz);

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
    eyes.reset();
    weather.reset();
    photo.reset();
    panorama.reset();
    city.reset();
    streamedLights.clear();
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
  // mirar) desde 'eye' mirando hacia 'forward' (unitaria). 'dt': para los "hold".
  void UpdateSight(Vector3 eye, Vector3 forward, float dt, const GameState& state,
                   std::vector<Action>& fired);
  // Entidades con "near": pone o quita su bandera (en 'fired') segun lo que lleva el jugador
  // (pies en 'feet') cerca o lejos de ellas.
  void UpdateNear(Vector3 feet, float dt, std::vector<Action>& fired);

  // Si algo solido corta el rayo antes de 'distance' (el refugio del viento).
  bool Blocked(Vector3 origin, Vector3 dir, float distance) const {
    return Occluded(origin, dir, distance, nullptr);
  }

private:
  // Si alguna entidad solida (que no sea 'self') corta el rayo antes de 'distance'.
  bool Occluded(Vector3 origin, Vector3 dir, float distance, const Entity* self) const;

  std::vector<std::unique_ptr<Entity>> entities_;
  bool triggersPrimed_ = false;
};

} // namespace retro