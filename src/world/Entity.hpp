#pragma once

#include "assets/Resources.hpp"
#include "graphics/Light.hpp"
#include "graphics/Transform.hpp"
#include "physics/Collision.hpp"
#include "world/Logic.hpp"

#include <raylib.h>

#include <cmath>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace retro {

struct EntityLight {
  Vector3 offset{0.0f, 0.0f, 0.0f};
  PointLight light;
};

struct EntitySound {
  Vector3 offset{0.0f, 0.0f, 0.0f};
  std::string file;
  float volume = 1.0f;
  float radius = 10.0f;
  float minDistance = 1.0f;
};

// Objeto del mundo: un Transform propio y, según el tipo, un modelo 3D
// compartido o un sprite billboard (textura + tamaño en el mundo).
// Bruma fija del shader (uniform fogFixed): >= 0 sustituye a la niebla por distancia (0 = sin
// niebla); -1 la devuelve. Para el panorama y las entidades con "fog": false.
void SetFogFixed(const Shader& shader, float fog);

class Entity {
public:
  using UpdateFn = std::function<void(Entity&, float dt)>;

  // Modelo. El shader retro debe estar ya asignado (ModelResource::SetShader).
  // Si se pasa textura, reemplaza el mapa difuso de los materiales sin textura propia
  // (ModelResource::OpenMaterials) al dibujar.
  Entity(std::string name, std::shared_ptr<ModelResource> model,
         std::shared_ptr<TextureResource> texture = nullptr);

  // Sprite billboard. 'size' = ancho y alto en unidades de mundo, antes de
  // transform.scale. transform.position es el punto bajo los pies (base centrada).
  Entity(std::string name, std::shared_ptr<TextureResource> sprite, Vector2 size);

  Entity(const Entity&) = delete;
  Entity& operator=(const Entity&) = delete;

  const std::string& Name() const noexcept { return name_; }
  bool IsSprite() const noexcept { return model_ == nullptr; }
  float SpriteHeight() const noexcept { return spriteSize_.y * std::fabs(transform.scale.y); }

  // Sprite animado: la imagen es una tira horizontal de 'frames' fotogramas iguales que se
  // pasan a 'fps' por segundo. 'phase' (segundos) desfasa unas copias de otras.
  void SetAnimation(int frames, float fps, float phase) noexcept {
    frames_ = frames;
    fps_ = fps;
    animTime_ = phase;
  }

  void SetUpdate(UpdateFn fn) { updateFn_ = std::move(fn); }
  void Update(float dt, Vector3 focus);

  // 'fullbright': shader sin iluminacion (el de los sprites) para los modelos emisivos.
  void Draw(const Shader& fullbright);
  void DrawSprite(const Camera3D& camera, Vector3 shade);

  BoundingBox LocalBounds();

  Transform transform;
  Color tint = WHITE;
  bool visible = true;
  bool emissive = false;
  // Capa de dibujo para superficies apoyadas en otras (modelos): 1 gana siempre a lo que tiene
  // debajo, -1 pierde. Ver depthBias en retro.vs.
  int layer = 0;
  // false: la niebla no la apaga (el sol, una luz que se ve de lejos).
  bool fog = true;
  bool facePlayer = false;
  float lookRange = 10.0f;
  float turnSpeed = 240.0f;
  std::vector<Condition> showIf;
  // Segundos de "interferencia" al aparecer o desaparecer por "showIf" (0 = de golpe).
  float glitch = 0.0f;
  // Lo que dicen sus condiciones ahora. Sin glitch cambia 'visible' al momento; con glitch, la
  // entidad parpadea y se deforma un rato antes de quedarse (o de irse del todo).
  void SetShown(bool shown);
  // Cajas de colision (vacia = se atraviesa). Varias si el modelo trae las suyas.
  std::vector<BoxCollider> colliders;
  std::optional<Interaction> interact;
  std::optional<Sight> sight;
  std::optional<EntityLight> light;
  std::optional<EntitySound> sound;

  struct Idle {
    float bob = 0.015f;
    float sway = 1.2f;
    float speed = 2.0f;
    float phase = 0.0f;
  };
  std::optional<Idle> idle;

  // Va y viene (suave) entre 'base' y base + 'offset', 'period' segundos la ida y la vuelta:
  // los patos del tiro al blanco, una barca que se mece. Mueve la posicion de verdad (la
  // colision y los disparos la siguen). 'phase' (0..1) desfasa unas de otras.
  struct Slide {
    Vector3 offset{0.0f, 0.0f, 0.0f};
    float period = 4.0f;
    float phase = 0.0f;
    Vector3 base{0.0f, 0.0f, 0.0f};
  };
  std::optional<Slide> slide;

  // Llega (una vez) cuando 'showIf' pasa a cumplirse: aparece en base + 'offset' y va hasta su
  // sitio en 'seconds' segundos, a velocidad constante y frenando de forma uniforme la
  // ultima fraccion 'brake' del tiempo (el tren en la estacion). Tiembla un poco mientras se
  // mueve. Si ya se cumplia al cargar la escena, esta en su sitio.
  struct Arrive {
    Vector3 offset{0.0f, 0.0f, 0.0f};
    float seconds = 6.0f;
    float brake = 0.5f;
    Vector3 base{0.0f, 0.0f, 0.0f};
  };
  std::optional<Arrive> arrive;

  // Pegada a otra entidad ('to'): la posicion y el giro en Y del JSON son relativos a ella, y la
  // sigue cuando se mueve o gira (Clara montada en el tiovivo, alguien en una barquilla de la
  // noria). Sigue derecha aunque la otra gire en X o Z. La resuelve Scene::Update.
  struct Attach {
    std::string to;
    Vector3 position{0.0f, 0.0f, 0.0f};
    float yaw = 0.0f;
    Entity* parent = nullptr; // se busca la primera vez (es de la misma escena)
  };
  std::optional<Attach> attach;

  // Humo o vapor (la chimenea del tren): bocanadas, sprites de 'texture' (tira de 4 fotogramas),
  // que salen de 'offset' (relativo a la entidad) a 'rate' por segundo, suben a 'rise' m/s con
  // algo de deriva y crecen de size.x a size.y durante 'life' segundos. Solo si es visible.
  struct Smoke {
    std::shared_ptr<TextureResource> texture;
    Vector3 offset{0.0f, 0.0f, 0.0f};
    float rate = 4.0f;
    float life = 2.5f;
    float rise = 1.2f;
    Vector2 size{0.5f, 1.8f};
    Color tint{200, 200, 205, 255};
  };
  std::optional<Smoke> smoke;
  bool HasPuffs() const noexcept { return !puffs_.empty(); }
  void DrawSmoke(const Camera3D& camera, Vector3 shade) const;

private:
  struct Puff {
    Vector3 position;
    float age;
    int frame;
    Vector2 drift; // deriva lateral (m/s)
  };
  std::vector<Puff> puffs_;
  float puffClock_ = 0.0f;
  unsigned puffCount_ = 0;
  float slideTime_ = 0.0f;
  float arriveTime_ = -1.0f; // negativo: no esta llegando
  std::string name_;
  float idleTime_ = 0.0f;
  int frames_ = 1;
  float fps_ = 8.0f;
  float animTime_ = 0.0f;
  Vector3 drawOffset_{0.0f, 0.0f, 0.0f};
  float drawRoll_ = 0.0f;
  bool shownKnown_ = false; // la primera vez se aplica sin efecto (al cargar la escena)
  bool shown_ = true;
  float glitchTime_ = -1.0f; // < 0: sin transicion en curso
  bool glitchOut_ = false;   // la transicion es de salida
  // Interferencia de este fotograma (0 = ninguna, 1 = maxima) y si se salta el dibujo.
  float GlitchAmount() const noexcept;
  std::shared_ptr<ModelResource> model_;
  std::shared_ptr<TextureResource> texture_;
  Vector2 spriteSize_{1.0f, 1.0f};
  UpdateFn updateFn_;
};

} // namespace retro