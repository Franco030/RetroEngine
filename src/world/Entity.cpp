#include "world/Entity.hpp"

#include "graphics/Billboard.hpp"

#include <rlgl.h>

#include <algorithm>
#include <cmath>
#include <stdexcept>
#include <utility>
#include <vector>

namespace retro {

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

void Entity::DrawSprite(const Camera3D& camera, Vector3 shade) {
  if (!visible || !IsSprite())
    return;

  const Vector2 size{spriteSize_.x * std::fabs(transform.scale.x),
                     spriteSize_.y * std::fabs(transform.scale.y)};

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