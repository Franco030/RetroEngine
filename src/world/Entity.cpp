#include "world/Entity.hpp"

#include "graphics/Billboard.hpp"

#include <rlgl.h>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <stdexcept>
#include <utility>
#include <vector>

namespace retro {

namespace {

// Numero pseudoaleatorio 0..1 repetible a partir de un entero (sin estado: mismo fotograma, mismo
// valor en todas las llamadas).
float Hash01(std::uint32_t x) {
  x ^= x >> 16;
  x *= 0x7feb352dU;
  x ^= x >> 15;
  x *= 0x846ca68bU;
  x ^= x >> 16;
  return static_cast<float>(x & 0xFFFFFFU) / static_cast<float>(0xFFFFFFU);
}

} // namespace

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

BoundingBox Entity::LocalBounds() {
  if (IsSprite()) {
    const float halfW = spriteSize_.x * 0.5f;
    const float halfD = spriteSize_.x * 0.2f;
    return {{-halfW, 0.0f, -halfD}, {halfW, spriteSize_.y, halfD}};
  }
  const Model& model = model_->Get();
  if (model.meshCount <= 0)
    return {{-0.5f, 0.0f, -0.5f}, {0.5f, 1.0f, 0.5f}};
  BoundingBox box = GetMeshBoundingBox(model.meshes[0]);
  for (int i = 1; i < model.meshCount; ++i) {
    const BoundingBox b = GetMeshBoundingBox(model.meshes[i]);
    box.min = {std::min(box.min.x, b.min.x), std::min(box.min.y, b.min.y),
               std::min(box.min.z, b.min.z)};
    box.max = {std::max(box.max.x, b.max.x), std::max(box.max.y, b.max.y),
               std::max(box.max.z, b.max.z)};
  }
  return box;
}

void Entity::Update(float dt, Vector3 focus) {
  if (updateFn_)
    updateFn_(*this, dt);
  if (glitchTime_ >= 0.0f) {
    glitchTime_ += dt;
    if (glitchTime_ >= glitch) {
      glitchTime_ = -1.0f;
      visible = shown_;
    }
  }
  if (frames_ > 1)
    animTime_ += dt;

  if (facePlayer) {
    const float dx = focus.x - transform.position.x;
    const float dz = focus.z - transform.position.z;
    const float dist2 = dx * dx + dz * dz;
    if (dist2 > 1e-4f && dist2 <= lookRange * lookRange) {
      const float target = std::atan2(dx, dz) * RAD2DEG;
      const float diff = std::remainder(target - transform.rotation.y, 360.0f);
      const float step = turnSpeed * dt;
      transform.rotation.y =
          std::remainder(transform.rotation.y + std::clamp(diff, -step, step), 360.0f);
    }
  }

  if (smoke) {
    // bocanadas nuevas (solo si se ve) y las que hay suben, derivan y envejecen
    if (visible) {
      puffClock_ += dt * smoke->rate;
      const Matrix m = transform.ToMatrix();
      while (puffClock_ >= 1.0f) {
        puffClock_ -= 1.0f;
        const std::uint32_t k = puffCount_++ * 3u + 7u;
        puffs_.push_back({Vector3Transform(smoke->offset, m), 0.0f, static_cast<int>(k % 4u),
                          {(Hash01(k) - 0.5f) * 0.5f, (Hash01(k + 1) - 0.5f) * 0.5f}});
      }
    }
    for (Puff& p : puffs_) {
      p.age += dt;
      p.position.x += p.drift.x * dt;
      p.position.z += p.drift.y * dt;
      p.position.y += smoke->rise * dt * (1.0f - 0.5f * p.age / smoke->life);
    }
    std::erase_if(puffs_, [&](const Puff& p) { return p.age >= smoke->life; });
  }

  if (slide) {
    slideTime_ += dt;
    const float u = slideTime_ / slide->period + slide->phase;
    const float k = 0.5f - 0.5f * std::cos(2.0f * PI * u);
    transform.position = Vector3Add(slide->base, Vector3Scale(slide->offset, k));
  }

  if (arrive && arriveTime_ >= 0.0f) {
    arriveTime_ += dt;
    const float u = arriveTime_ / arrive->seconds;
    if (u >= 1.0f) {
      transform.position = arrive->base;
      arriveTime_ = -1.0f;
    } else {
      // lo que falta del recorrido (1 a 0): recta a velocidad constante y frenada uniforme
      const float b = arrive->brake;
      const float cruise = 1.0f - b;
      const float total = cruise + b * 0.5f;
      const float left = u < cruise ? 1.0f - u / total : (1.0f - u) * (1.0f - u) / (2.0f * b * total);
      const float speed = u < cruise ? 1.0f : (1.0f - u) / b;
      transform.position = Vector3Add(arrive->base, Vector3Scale(arrive->offset, left));
      transform.position.y += std::sin(arriveTime_ * 31.0f) * 0.012f * speed; // el traqueteo
    }
  }

  if (idle) {
    idleTime_ += dt;
    const float t = idleTime_ * idle->speed * 3.0f + idle->phase;
    drawOffset_ = {0.0f, std::fabs(std::sin(t)) * idle->bob, 0.0f};
    drawRoll_ = std::sin(t * 0.5f) * idle->sway;
  }
}

namespace {

// Paso de cada capa en profundidad NDC: crece con la distancia como el error de profundidad,
// y de cerca equivale a pocos centimetros (no se ve "a traves" de nada).
constexpr float kLayerStep = 3.0e-5f;

void SetDepthBias(const Shader& shader, float bias) {
  const int loc = GetShaderLocation(shader, "depthBias");
  if (loc >= 0)
    SetShaderValue(shader, loc, &bias, SHADER_UNIFORM_FLOAT);
}

} // namespace


void Entity::SetShown(bool shown) {
  if (arrive && shownKnown_ && shown && !shown_) {
    arriveTime_ = 0.0f;
    transform.position = Vector3Add(arrive->base, arrive->offset);
  }
  if (glitch <= 0.0f || !shownKnown_) {
    visible = shown;
    shown_ = shown;
    shownKnown_ = true;
    return;
  }
  if (shown == shown_)
    return;
  shown_ = shown;
  glitchTime_ = 0.0f;
  glitchOut_ = !shown;
  visible = true; // durante la salida sigue dibujandose (parpadeando) hasta el final
}

float Entity::GlitchAmount() const noexcept {
  if (glitchTime_ < 0.0f)
    return 0.0f;
  const float t = std::clamp(glitchTime_ / glitch, 0.0f, 1.0f);
  return glitchOut_ ? t : 1.0f - t * t; // al llegar se asienta deprisa; al irse, cada vez peor
}

void SetFogFixed(const Shader& shader, float fog) {
  const int loc = GetShaderLocation(shader, "fogFixed");
  if (loc >= 0)
    SetShaderValue(shader, loc, &fog, SHADER_UNIFORM_FLOAT);
}

void Entity::Draw(const Shader& fullbright) {
  if (!visible || IsSprite())
    return;

  Model& model = model_->Get();

  // El modelo es compartido: se configura justo antes de cada dibujado. La textura de la
  // entidad va solo a los materiales sin textura propia; sin "texture", esos quedan en blanco
  // (si no, heredarian la de la ultima entidad que dibujo el mismo modelo).
  const Texture2D white{rlGetTextureIdDefault(), 1, 1, 1, PIXELFORMAT_UNCOMPRESSED_R8G8B8A8};
  const Texture2D& tex = texture_ ? texture_->Get() : white;
  for (int m : model_->OpenMaterials()) {
    SetMaterialTexture(&model.materials[m], MATERIAL_MAP_DIFFUSE, tex);
  }

  Transform visual = transform;
  visual.position = Vector3Add(visual.position, drawOffset_);
  visual.rotation.z += drawRoll_;
  if (const float k = GlitchAmount(); k > 0.0f) {
    // Interferencia: a saltos (unos 30 por segundo) no se dibuja, se estira, se encoge y se
    // desplaza de lado; cuanto mayor 'k', mas a menudo y mas fuerte.
    const auto paso = static_cast<std::uint32_t>(glitchTime_ * 30.0f) * 4u +
                      static_cast<std::uint32_t>(reinterpret_cast<std::uintptr_t>(this) >> 4);
    if (Hash01(paso) < 0.5f * k)
      return;
    visual.scale.x *= 1.0f - 0.65f * k * Hash01(paso + 1);
    visual.scale.y *= 1.0f + 0.45f * k * (Hash01(paso + 2) - 0.3f);
    visual.position.x += (Hash01(paso + 3) - 0.5f) * 0.5f * k;
    visual.position.z += (Hash01(paso + 4) - 0.5f) * 0.3f * k;
  }
  model.transform = visual.ToMatrix();
  if (layer != 0)
    SetDepthBias(emissive ? fullbright : model.materials[0].shader, layer * kLayerStep);
  if (!fog)
    SetFogFixed(emissive ? fullbright : model.materials[0].shader, 0.0f);
  if (!emissive) {
    DrawModel(model, {0.0f, 0.0f, 0.0f}, 1.0f, tint);
    if (layer != 0)
      SetDepthBias(model.materials[0].shader, 0.0f);
    if (!fog)
      SetFogFixed(model.materials[0].shader, -1.0f);
    return;
  }
  // Emisivo (una vidriera, algo que brilla): se dibuja con el shader de los sprites, que usa la
  // textura tal cual, sin luces. El modelo es compartido: se devuelve su shader al terminar.
  std::vector<Shader> lit(static_cast<std::size_t>(model.materialCount));
  for (int m = 0; m < model.materialCount; ++m) {
    lit[static_cast<std::size_t>(m)] = model.materials[m].shader;
    model.materials[m].shader = fullbright;
  }
  DrawModel(model, {0.0f, 0.0f, 0.0f}, 1.0f, tint);
  for (int m = 0; m < model.materialCount; ++m)
    model.materials[m].shader = lit[static_cast<std::size_t>(m)];
  if (layer != 0)
    SetDepthBias(fullbright, 0.0f);
  if (!fog)
    SetFogFixed(fullbright, -1.0f);
}

void Entity::DrawSmoke(const Camera3D& camera, Vector3 shade) const {
  if (!smoke || !smoke->texture)
    return;
  auto channel = [](unsigned char t, float f) {
    return static_cast<unsigned char>(std::min(255.0f, static_cast<float>(t) * f));
  };
  const Color c{channel(smoke->tint.r, shade.x), channel(smoke->tint.g, shade.y),
                channel(smoke->tint.b, shade.z), smoke->tint.a};
  for (const Puff& p : puffs_) {
    const float t = p.age / smoke->life;
    const float s = smoke->size.x + (smoke->size.y - smoke->size.x) * t;
    const Vector3 base{p.position.x, p.position.y - s * 0.5f, p.position.z};
    const float u0 = static_cast<float>(p.frame) / 4.0f;
    DrawUprightBillboard(camera, smoke->texture->Get(), base, {s, s}, c, u0, u0 + 0.25f);
  }
}

void Entity::DrawSprite(const Camera3D& camera, Vector3 shade) {
  if (!visible || !IsSprite())
    return;

  Vector2 size{spriteSize_.x * std::fabs(transform.scale.x),
               spriteSize_.y * std::fabs(transform.scale.y)};
  if (const float g = GlitchAmount(); g > 0.0f) {
    // la misma interferencia que en Draw, sin desplazamiento (el billboard ya mira a la camara)
    const auto paso = static_cast<std::uint32_t>(glitchTime_ * 30.0f) * 4u +
                      static_cast<std::uint32_t>(reinterpret_cast<std::uintptr_t>(this) >> 4);
    if (Hash01(paso) < 0.5f * g)
      return;
    size.x *= 1.0f - 0.65f * g * Hash01(paso + 1);
    size.y *= 1.0f + 0.45f * g * (Hash01(paso + 2) - 0.3f);
  }

  const Vector3 k = emissive ? Vector3{1.0f, 1.0f, 1.0f} : shade;
  auto channel = [](unsigned char t, float f) {
    return static_cast<unsigned char>(std::min(255.0f, static_cast<float>(t) * f));
  };
  const Color c{channel(tint.r, k.x), channel(tint.g, k.y), channel(tint.b, k.z), tint.a};

  // fotograma actual de la tira (sin mezclar: salta de uno a otro, como en la PS1)
  const int frame = static_cast<int>(animTime_ * fps_) % frames_;
  const float u0 = static_cast<float>(frame) / static_cast<float>(frames_);
  const float u1 = static_cast<float>(frame + 1) / static_cast<float>(frames_);
  DrawUprightBillboard(camera, texture_->Get(), transform.position, size, c, u0, u1);
}

} // namespace retro