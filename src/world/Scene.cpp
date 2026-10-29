#include "world/Scene.hpp"
#include "graphics/ShaderScope.hpp"

#include <rlgl.h>

#include <raymath.h>

#include <algorithm>
#include <cmath>
#include <cfloat>
#include <stdexcept>
#include <utility>

namespace retro {

namespace {

// Los "once" se recuerdan en el GameState, así sobreviven a cambios de escena.
std::string OnceKey(const std::string& scenePath, const std::string& triggerName) {
  return "once:" + scenePath + "#" + triggerName;
}

std::string InteractKey(const std::string& scenePath, const std::string& entityName) {
  return "interact:" + scenePath + "#" + entityName;
}

// Cuatro muros por FUERA del rectángulo. Los de norte y sur se alargan para
// cubrir las esquinas, y su grosor (2) es mucho mayor que el avance por subpaso,
// así que no se pueden atravesar.
void AppendBoundsWalls(const Scene::Bounds& b, std::vector<WorldBox>& out) {
  constexpr float kThick = 2.0f;
  constexpr float kBottom = -1.0f;
  constexpr float kTop = 10.0f; // mucho más alto que un salto

  const float cx = (b.min.x + b.max.x) * 0.5f;
  const float cz = (b.min.y + b.max.y) * 0.5f;
  const float hx = (b.max.x - b.min.x) * 0.5f;
  const float hz = (b.max.y - b.min.y) * 0.5f;
  const float t = kThick * 0.5f;

  auto wall = [&](float x, float z, float halfX, float halfZ) {
    WorldBox w; // ejes por defecto: alineada con X y Z
    w.center = {x, 0.0f, z};
    w.halfX = halfX;
    w.halfZ = halfZ;
    w.bottom = kBottom;
    w.top = kTop;
    out.push_back(w);
  };

  wall(b.min.x - t, cz, t, hz + kThick); // oeste
  wall(b.max.x + t, cz, t, hz + kThick); // este
  wall(cx, b.min.y - t, hx + kThick, t); // norte (-Z)
  wall(cx, b.max.y + t, hx + kThick, t); // sur (+Z)
}

} // namespace

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
  return std::erase_if(entities_, [&](const auto& e) { return e->Name() == name; }) > 0;
}

void Scene::Update(float dt, Vector3 focus) {
  for (auto& e : entities_)
    e->Update(dt, focus);
}

void Scene::CollectLights(std::vector<PointLight>& out) const {
  const double time = GetTime();

  auto push = [&](PointLight l) {
    const float f = FlickerFactor(l, time);
    l.color = {l.color.x * f, l.color.y * f, l.color.z * f};
    l.radius *= 0.85f + 0.15f * f;
    out.push_back(l);
  };

  for (const PointLight& l : lights)
    push(l);

  for (const auto& e : entities_) {
    if (!e->light || !e->visible)
      continue;

    PointLight l = e->light->light;
    const Vector3& o = e->light->offset;
    const Vector3& s = e->transform.scale;
    const Vector3& p = e->transform.position;
    l.position = {p.x + o.x * s.x, p.y + o.y * s.y, p.z + o.z * s.z};
    push(l);
  }
}

void Scene::CollectEmitters(std::vector<Emitter>& out) const {
  out.insert(out.end(), emitters.begin(), emitters.end());

  for (const auto& e : entities_) {
    if (!e->sound || !e->visible)
      continue;

    const EntitySound& s = *e->sound;
    const Vector3& p = e->transform.position;
    const Vector3& sc = e->transform.scale;

    Emitter em;
    em.key = "entity:" + e->Name();
    em.file = s.file;
    em.position = {p.x + s.offset.x * sc.x, p.y + s.offset.y * sc.y, p.z + s.offset.z * sc.z};
    em.volume = s.volume;
    em.radius = s.radius;
    em.minDistance = s.minDistance;
    out.push_back(em);
  }
}

void Scene::DrawPanorama(const Camera3D& camera, const Shader& spriteShader) const {
  const Panorama& p = *panorama;
  Model& model = p.model->Get();
  const Shader lit = model.materials[0].shader;

  SetFogFixed(lit, p.haze);
  model.transform = MatrixTranslate(camera.position.x, p.y, camera.position.z);
  DrawModel(model, {0.0f, 0.0f, 0.0f}, 1.0f, p.tint);
  SetFogFixed(lit, -1.0f);

  if (!p.sun)
    return;
  // Mas lejos que el modelo (que acaba a ~200 m): las montanas lo tapan por profundidad.
  constexpr float kSunDistance = 320.0f;
  const Vector3 pos{camera.position.x + p.sunDirection.x * kSunDistance,
                    camera.position.y + p.sunDirection.y * kSunDistance,
                    camera.position.z + p.sunDirection.z * kSunDistance};
  ShaderScope scope(spriteShader);
  if (const int loc = GetShaderLocation(spriteShader, "matModel"); loc >= 0)
    SetShaderValueMatrix(spriteShader, loc, MatrixIdentity());
  SetFogFixed(spriteShader, 0.0f); // el sol no se apaga con la niebla
  DrawBillboard(camera, p.sun->Get(), pos, p.sunSize, WHITE);
  rlDrawRenderBatchActive(); // que se dibuje antes de devolver el uniform
  SetFogFixed(spriteShader, -1.0f);
}

void Scene::Draw(const Camera3D& camera, const Shader& spriteShader, float ambient, float sun,
                 std::span<const PointLight> lights) {
  if (panorama)
    DrawPanorama(camera, spriteShader);

  bool anySprite = false;
  for (auto& e : entities_) {
    if (e->IsSprite()) {
      anySprite = anySprite || e->visible;
    } else {
      e->Draw(spriteShader);
    }
  }
  if (!anySprite)
    return;

  const float base = ambient + (1.0f - ambient) * 0.5f * sun;

  ShaderScope scope(spriteShader);
  // Los billboards se mandan ya en coordenadas de mundo: matriz de modelo identidad (si no, el
  // shader se quedaria con la del ultimo modelo emisivo dibujado y fragWorldY saldria mal).
  if (const int loc = GetShaderLocation(spriteShader, "matModel"); loc >= 0)
    SetShaderValueMatrix(spriteShader, loc, MatrixIdentity());
  for (auto& e : entities_) {
    if (!e->IsSprite() || !e->visible)
      continue;

    const Vector3 center{e->transform.position.x,
                         e->transform.position.y + e->SpriteHeight() * 0.5f,
                         e->transform.position.z};
    Vector3 shade{base, base, base};
    for (const PointLight& l : lights) {
      const float t = std::clamp(1.0f - Vector3Distance(l.position, center) / l.radius, 0.0f, 1.0f);
      const float k = t * t * 0.6f;
      shade = {shade.x + l.color.x * k, shade.y + l.color.y * k, shade.z + l.color.z * k};
    }
    if (!e->fog) { // los sprites van en lote: se vacia antes y despues de cambiar el uniform
      rlDrawRenderBatchActive();
      SetFogFixed(spriteShader, 0.0f);
    }
    e->DrawSprite(camera, shade);
    if (!e->fog) {
      rlDrawRenderBatchActive();
      SetFogFixed(spriteShader, -1.0f);
    }
  }
}

void Scene::CollectColliders(std::vector<WorldBox>& out) const {
  for (const auto& e : entities_) {
    if (!e->visible)
      continue;
    for (const BoxCollider& c : e->colliders)
      out.push_back(MakeWorldBox(c, e->transform));
  }

  if (bounds)
    AppendBoundsWalls(*bounds, out);
}

void Scene::RefreshVisibility(const GameState& state) {
  for (auto& e : entities_) {
    if (!e->showIf.empty())
      e->SetShown(AllHold(e->showIf, state));
  }
}

void Scene::UpdateTriggers(Vector3 feet, float radius, float height, GameState& state,
                           std::vector<Action>& fired) {
  // La primera llamada tras cargar solo registra donde esta el jugador
  for (Trigger& t : triggers) {
    const bool verticalOk = feet.y + height > t.box.bottom && feet.y < t.box.top;
    const bool now = verticalOk && OverlapsCircle(t.box, {feet.x, feet.z}, radius);
    const bool was = t.inside;
    t.inside = now;

    if (!triggersPrimed_)
      continue;

    bool event = false;
    switch (t.on) {
    case TriggerEvent::Enter:
      event = now && !was;
      break;
    case TriggerEvent::Exit:
      event = !now && was;
      break;
    case TriggerEvent::Stay:
      event = now;
      break;
    }
    if (!event)
      continue;

    if (t.once && state.Get(OnceKey(path, t.name)) != 0)
      continue;
    if (!AllHold(t.conditions, state))
      continue; // no consume el "once"

    if (t.once)
      state.Set(OnceKey(path, t.name), 1);
    fired.insert(fired.end(), t.actions.begin(), t.actions.end());
  }

  triggersPrimed_ = true;
}

bool Scene::Occluded(Vector3 origin, Vector3 dir, float distance, const Entity* self) const {
  for (const auto& o : entities_) {
    if (o.get() == self || !o->visible)
      continue;
    for (const BoxCollider& c : o->colliders) {
      float to = 0.0f;
      if (RayIntersectsBox(MakeWorldBox(c, o->transform), origin, dir, to) && to < distance - 1e-3f)
        return true;
    }
  }
  return false;
}

// Mitad de la altura de la vista (60 grados de campo vertical), con algo de margen; y para
// "fuera de pantalla", algo mas.
constexpr float kSightVertical = 35.0f * DEG2RAD;
constexpr float kOffScreenVertical = 45.0f * DEG2RAD;

void Scene::UpdateSight(Vector3 eye, Vector3 forward, std::vector<Action>& fired) {
  for (auto& e : entities_) {
    if (!e->sight || !e->visible)
      continue;
    Sight& s = *e->sight;
    if (s.seen && s.away)
      continue;

    // Se mira al centro de su caja (no a los pies): el cuerpo, la cara.
    const BoundingBox lb = e->LocalBounds();
    BoxCollider box;
    box.min = lb.min;
    box.max = lb.max;
    const WorldBox wb = MakeWorldBox(box, e->transform);
    const Vector3 center{wb.center.x, (wb.bottom + wb.top) * 0.5f, wb.center.z};
    const Vector3 to = Vector3Subtract(center, eye);
    const float dist = Vector3Length(to);
    // Vista: el angulo se mide en horizontal (hacia donde se gira la cabeza) y en vertical basta
    // con que quepa en pantalla: quien mira algo bajo o alto, tambien lo ve.
    // "La ves" (cerca del centro, 'angle') no es lo contrario de "ya no la ves": eso es que este
    // fuera de pantalla ('awayAngle', mas ancho que el campo de vision), lejos o tapada. Si no,
    // lo que pasa al dejar de mirarla (algo que desaparece o se mueve) se veria por el borde.
    bool inView = false;
    bool onScreen = false;
    if (dist > 1e-3f && dist <= s.range) {
      const float flat = std::hypot(to.x, to.z);
      const float fflat = std::hypot(forward.x, forward.z);
      if (flat > 1e-3f && fflat > 1e-3f) {
        const float cosYaw = (to.x * forward.x + to.z * forward.z) / (flat * fflat);
        const float vertical = std::fabs(std::atan2(to.y, flat) - std::atan2(forward.y, fflat));
        onScreen = cosYaw >= std::cos(s.awayAngle * DEG2RAD) && vertical <= kOffScreenVertical &&
                   !Occluded(eye, Vector3Scale(to, 1.0f / dist), dist, e.get());
        inView = onScreen && cosYaw >= std::cos(s.angle * DEG2RAD) && vertical <= kSightVertical;
      }
    }

    if (!s.seen && inView) {
      s.seen = true;
      fired.insert(fired.end(), s.onSeen.begin(), s.onSeen.end());
    } else if (s.seen && !s.away && !onScreen) {
      s.away = true;
      fired.insert(fired.end(), s.onLookAway.begin(), s.onLookAway.end());
    }
  }
}

Scene::InteractHit Scene::FindInteractable(Vector3 origin, Vector3 dir, const GameState& state) {
  InteractHit best;
  float bestT = FLT_MAX;

  for (const auto& e : entities_) {
    if (!e->interact || !e->visible)
      continue;
    const Interaction& it = *e->interact;

    if (it.once && state.Get(InteractKey(path, e->Name())) != 0)
      continue;
    if (!AllHold(it.conditions, state))
      continue;

    float t = 0.0f;
    if (!RayIntersectsBox(MakeWorldBox(it.box, e->transform), origin, dir, t))
      continue;
    if (t > it.range || t >= bestT)
      continue;

    // Oclusión: cualquier OTRA entidad sólida más cerca bloquea el rayo.
    // La propia se excluye para que su collider no se tape a sí mismo.
    if (Occluded(origin, dir, t, e.get()))
      continue;

    best.entity = e.get();
    best.distance = t;
    bestT = t;
  }
  return best;
}

void Scene::Interact(Entity& entity, GameState& state, std::vector<Action>& fired) {
  if (!entity.interact)
    return;
  if (entity.interact->once)
    state.Set(InteractKey(path, entity.Name()), 1);
  fired.insert(fired.end(), entity.interact->actions.begin(), entity.interact->actions.end());
}

} // namespace retro