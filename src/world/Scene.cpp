#include "world/Scene.hpp"

#include <algorithm>
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

void Scene::Update(float dt) {
  for (auto& e : entities_)
    e->Update(dt);
}

void Scene::Draw() {
  for (auto& e : entities_)
    e->Draw();
}

void Scene::CollectColliders(std::vector<WorldBox>& out) const {
  for (const auto& e : entities_) {
    if (e->collider && e->visible) {
      out.push_back(MakeWorldBox(*e->collider, e->transform));
    }
  }

  if (bounds)
    AppendBoundsWalls(*bounds, out);
}

void Scene::UpdateTriggers(Vector3 feet, float radius, float height, GameState& state,
                           std::vector<Action>& fired) {
  // Primera llamada tras cargar: reaplica el estado persistente y no dispara nada.
  if (!triggersPrimed_) {
    // Un "once" ya disparado reaplica sus set_visible: lo recogido no reaparece
    // al volver a la escena, porque las entidades se recrean desde el JSON.
    auto reapply = [&](const std::vector<Action>& actions) {
      for (const Action& a : actions) {
        if (const auto* v = std::get_if<action::SetVisible>(&a)) {
          if (Entity* e = Find(v->entity))
            e->visible = v->visible;
        }
      }
    };

    for (const Trigger& t : triggers) {
      if (t.once && state.Get(OnceKey(path, t.name)) != 0)
        reapply(t.actions);
    }
    for (const auto& e : entities_) {
      if (e->interact && e->interact->once && state.Get(InteractKey(path, e->Name())) != 0) {
        reapply(e->interact->actions);
      }
    }
  }

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
    bool blocked = false;
    for (const auto& o : entities_) {
      if (o.get() == e.get() || !o->collider || !o->visible)
        continue;
      float to = 0.0f;
      if (RayIntersectsBox(MakeWorldBox(*o->collider, o->transform), origin, dir, to) &&
          to < t - 1e-3f) {
        blocked = true;
        break;
      }
    }
    if (blocked)
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