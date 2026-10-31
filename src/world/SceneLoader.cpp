#include "world/SceneLoader.hpp"

#include "core/RetroEngine.hpp"
#include "graphics/MeshFactory.hpp"
#include "graphics/RetroCamera.hpp"
#include "physics/Collision.hpp"
#include "world/Entity.hpp"
#include "world/Scene.hpp"
#include "world/Trigger.hpp"

#include <raylib.h>
#include <raymath.h>

#include <nlohmann/json.hpp>

#include <algorithm>
#include <cmath>
#include <fstream>
#include <functional>
#include <memory>
#include <optional>
#include <stdexcept>
#include <string>

namespace retro {

namespace {

using json = nlohmann::json;

Vector2 ReadVec2(const json& j, const char* key) {
  const json& a = j.at(key);
  if (!a.is_array() || a.size() != 2) {
    throw std::runtime_error(std::string("'") + key + "' debe ser un arreglo de 2 numeros [x, z]");
  }
  return {a[0].get<float>(), a[1].get<float>()};
}

Vector3 ReadVec3(const json& j, const char* key, Vector3 fallback) {
  if (!j.contains(key))
    return fallback;

  const json& a = j.at(key);
  if (!a.is_array() || a.size() != 3) {
    throw std::runtime_error(std::string("'") + key + "' debe ser un arreglo de numeros.");
  }
  return {a[0].get<float>(), a[1].get<float>(), a[2].get<float>()};
}

Color ReadColor(const json& j, const char* key, Color fallback) {
  if (!j.contains(key))
    return fallback;

  const json& a = j.at(key);
  if (!a.is_array() || (a.size() != 3 && a.size() != 4)) {
    throw std::runtime_error(std::string("'") + key + "' debe ser [r,g,b] o [r,g,b,a]");
  }
  return {a[0].get<unsigned char>(), a[1].get<unsigned char>(), a[2].get<unsigned char>(),
          a.size() == 4 ? a[3].get<unsigned char>() : static_cast<unsigned char>(255)};
}

// { "color": [r,g,b], "intensity": 1.0, "radius": 8 }; la posición la pone quien llama.
PointLight ReadPointLight(const json& j, Vector3 position) {
  PointLight l;
  l.position = position;

  const Color c = ReadColor(j, "color", WHITE);
  const float k = j.value("intensity", 1.0f);
  l.color = {static_cast<float>(c.r) / 255.0f * k, static_cast<float>(c.g) / 255.0f * k,
             static_cast<float>(c.b) / 255.0f * k};
  l.radius = j.value("radius", 8.0f);
  if (l.radius <= 0.0f)
    throw std::runtime_error("'radius' debe ser positivo");

  l.flicker = std::clamp(j.value("flicker", 0.0f), 0.0f, 1.0f);
  l.flickerRate = j.value("flickerRate", 10.0f);
  if (l.flickerRate <= 0.0f)
    throw std::runtime_error("'flickerRate' debe ser positivo");

  return l;
}

std::shared_ptr<ModelResource> LoadModelSpec(const json& spec, RetroEngine& engine) {
  std::shared_ptr<ModelResource> model;

  if (spec.is_string()) {
    model = engine.Assets().GetModel(spec.get<std::string>());
  } else if (spec.is_object()) {
    const std::string type = spec.at("type").get<std::string>();
    if (type == "plane") {
      model = std::make_shared<ModelResource>(MakeTiledPlane(
          spec.value("size", 10.0f), spec.value("size", 10.0f), spec.value("subdivisions", 1),
          spec.value("tiles", 1.0f), spec.value("tiles", 1.0f)));
    } else {
      throw std::runtime_error("tipo de modelo desconocido: '" + type + "' (plane)");
    }
  } else {
    throw std::runtime_error("'model' debe ser una ruta o un objeto");
  }

  model->SetShader(engine.Retro().Lit());
  return model;
}

int ParseWrap(const std::string& w) {
  if (w == "repeat")
    return TEXTURE_WRAP_REPEAT;
  if (w == "clamp")
    return TEXTURE_WRAP_CLAMP;
  if (w == "mirror")
    return TEXTURE_WRAP_MIRROR_REPEAT;

  throw std::runtime_error("wrap desconocido: '" + w + "' (repeat, clamp o mirror)");
}

BoxCollider ComputeAutoBox(ModelResource& resource) {
  const Model& model = resource.Get();
  if (model.meshCount <= 0) {
    throw std::runtime_error("el modelo no tiene mallas para calcular el collider");
  }
  BoundingBox total = GetMeshBoundingBox(model.meshes[0]);
  for (int i = 1; i < model.meshCount; ++i) {
    const BoundingBox b = GetMeshBoundingBox(model.meshes[i]);
    total.min = Vector3Min(total.min, b.min);
    total.max = Vector3Max(total.max, b.max);
  }
  return {total.min, total.max};
}

BoxCollider SpriteBox(Vector2 size) {
  const float half = size.x * 0.2f;
  return {{-half, 0.0f, -half}, {half, size.y, half}};
}

// Cajas automaticas de un modelo: las que trae su .obj ("# retro:col"), o la caja de todo el
// modelo si no trae ninguna.
std::vector<BoxCollider> ModelColliders(const ModelResource& model, const BoxCollider& autoBox) {
  std::vector<BoxCollider> out;
  for (const ModelBox& b : model.CollisionBoxes())
    out.push_back({b.min, b.max, b.yaw});
  if (out.empty())
    out.push_back(autoBox);
  return out;
}

// "collider": true | false | "auto" | { "min": [x,y,z], "max": [x,y,z], "yaw": 0 }
// true / "auto": las cajas automaticas (las del modelo, o la de todo el modelo).
// Sin el campo: las automaticas solo si la escena tiene autoCollide y el modelo es un archivo
// .obj (los planos procedurales usan el suelo Y=0).
std::vector<BoxCollider> ReadCollider(const json& je, const std::vector<BoxCollider>& autoBoxes,
                                      bool isMeshFile, bool autoCollide) {
  if (je.contains("collider")) {
    const json& jc = je.at("collider");

    if (jc.is_boolean())
      return jc.get<bool>() ? autoBoxes : std::vector<BoxCollider>{};
    if (jc.is_string() && jc.get<std::string>() == "auto")
      return autoBoxes;

    if (jc.is_object()) {
      if (!jc.contains("min") || !jc.contains("max")) {
        throw std::runtime_error("'collider' necesita 'min' y 'max'");
      }
      BoxCollider box;
      box.min = ReadVec3(jc, "min", {0.0f, 0.0f, 0.0f});
      box.max = ReadVec3(jc, "max", {0.0f, 0.0f, 0.0f});
      box.yaw = jc.value("yaw", 0.0f);
      if (box.min.x >= box.max.x || box.min.y >= box.max.y || box.min.z >= box.max.z) {
        throw std::runtime_error(
            "'collider': cada componente de 'min' debe ser menor que el de 'max'");
      }
      return {box};
    }
    throw std::runtime_error("'collider' debe ser true, false, \"auto\" o {min, max}");
  }

  if (autoCollide && isMeshFile)
    return autoBoxes;
  return {};
}

// Zona en el mundo a partir de "position" (centro bajo los pies), "size" y "rotation"
WorldBox ReadZone(const json& j) {
  const Vector3 size = ReadVec3(j, "size", {2.0f, 2.4f, 0.8f});
  if (size.x <= 0.0f || size.y <= 0.0f || size.z <= 0.0f) {
    throw std::runtime_error("'size' debe tener valores positivos");
  }

  BoxCollider local;
  local.min = {-size.x * 0.5f, 0.0f, -size.z * 0.5f};
  local.max = {size.x * 0.5f, size.y, size.z * 0.5f};

  retro::Transform t;
  t.position = ReadVec3(j, "position", {0.0f, 0.0f, 0.0f});
  t.rotation = {0.0f, j.value("rotation", 0.0f), 0.0f};
  return MakeWorldBox(local, t);
}

void EnsureUniqueTrigger(const Scene& scene, const std::string& name) {
  for (const Trigger& t : scene.triggers) {
    if (t.name == name)
      throw std::runtime_error("nombre de trigger duplicado: '" + name + "'");
  }
}

CompareOp ParseOp(const std::string& s) {
  if (s == "==")
    return CompareOp::Eq;
  if (s == "!=")
    return CompareOp::Ne;
  if (s == "<")
    return CompareOp::Lt;
  if (s == "<=")
    return CompareOp::Le;
  if (s == ">")
    return CompareOp::Gt;
  if (s == ">=")
    return CompareOp::Ge;
  throw std::runtime_error("operador desconocido: '" + s + "' (==, !=, <, <=, >, >=)");
}

// Una condicion es UNA de:
//   { "flag": "monedas", "op": ">=", "value": 3 }      (op por defecto "==", value por defecto 1)
//   { "all": [ ... ] }   { "any": [ ... ] }   { "not": { ... } }
Condition ParseCondition(const json& jc, int depth = 0) {
  if (!jc.is_object())
    throw std::runtime_error("cada condicion debe ser un objeto");
  if (depth > 8)
    throw std::runtime_error("condiciones anidadas demasiado profundas");

  const bool isNot = jc.contains("not");
  const int found = static_cast<int>(jc.contains("flag")) + static_cast<int>(jc.contains("all")) +
                    static_cast<int>(jc.contains("any")) + static_cast<int>(isNot);
  if (found != 1) {
    throw std::runtime_error("cada condicion necesita exactamente una de: flag, all, any, not");
  }

  Condition c;
  auto readChildren = [&](const char* key) {
    const json& arr = jc.at(key);
    if (!arr.is_array() || arr.empty()) {
      throw std::runtime_error(std::string("'") + key + "' debe ser un arreglo no vacio");
    }
    for (const json& item : arr)
      c.children.push_back(ParseCondition(item, depth + 1));
  };

  if (jc.contains("all")) {
    c.kind = Condition::Kind::All;
    readChildren("all");
  } else if (jc.contains("any")) {
    c.kind = Condition::Kind::Any;
    readChildren("any");
  } else if (isNot) {
    c.kind = Condition::Kind::Not;
    c.children.push_back(ParseCondition(jc.at("not"), depth + 1));
  } else {
    c.flag = jc.at("flag").get<std::string>();
    c.op = ParseOp(jc.value("op", std::string("==")));
    c.value = jc.value("value", 1);
  }
  return c;
}

} // namespace

// <key>: { ... }  o  <key>: [ { ... }, { ... } ]   (todas deben cumplirse)
std::vector<Condition> ParseConditions(const json& j, const char* key) {
  std::vector<Condition> out;
  if (!j.contains(key))
    return out;

  const json& jc = j.at(key);
  if (jc.is_object()) {
    out.push_back(ParseCondition(jc));
  } else if (jc.is_array()) {
    for (const json& item : jc)
      out.push_back(ParseCondition(item));
  } else {
    throw std::runtime_error(std::string("'") + key +
                             "' debe ser un objeto o un arreglo de objetos");
  }
  return out;
}

// Cada accion es un objeto con UNA clave de accion y, si hace falta, modificadores:
//   { "goto_scene": "scenes/x.json", "position": [x,y,z], "yaw": 0,
//     "dark": 10, "lines": ["...", "..."] }      (un rato en negro, con frases, antes de llegar)
//   { "teleport": [x,y,z], "yaw": 90 }
//   { "set_flag": "nombre", "value": 1 }
//   { "message": "texto\notra linea", "seconds": 3 }
//   { "dialogue": "sepulturero" }                      (assets/dialogues/sepulturero.json)
//   { "look_at": "entidad" | [x,y,z], "speed": 6 }              (mirar, sin acercar)
//   { "scare": "entidad" | [x,y,z], "speed": 20, "fov": 30 }    (susto: de golpe y acercando)
//   { "lock_movement": true | false }                  (mirar sin poder andar; o volver a andar)
//   { "title_screen": true }                           (con un goto_scene: llegar al menu)
//   { "ride": "entidad", "seat": [x,y,z], "exit": [x,y,z], "seconds": 40, "flag": "f", "after": "dialogo",
//     "locked": true }                                 (locked: E no baja, hay que esperar)
//   { "behind_player": "entidad", "distance": 2.4, "side": 0 }  (a la espalda del jugador)
//   { "shooting": "pato_", "position": [x,y,z], "yaw": 0, "seconds": 20, "flag": "puntos" }
Action ParseAction(const json& ja) {
  if (!ja.is_object())
    throw std::runtime_error("cada accion debe ser un objeto");

  Action result;
  int found = 0;

  if (ja.contains("goto_scene")) {
    action::GotoScene a;
    a.scene = ja.at("goto_scene").get<std::string>();
    if (ja.contains("position"))
      a.position = ReadVec3(ja, "position", {0.0f, 0.0f, 0.0f});
    if (ja.contains("yaw"))
      a.yaw = ja.at("yaw").get<float>();
    a.dark = ja.value("dark", 0.0f);
    if (ja.contains("lines"))
      a.lines = ja.at("lines").get<std::vector<std::string>>();
    if (a.dark < 0.0f || a.dark > 60.0f)
      throw std::runtime_error("goto_scene: 'dark' debe ir de 0 a 60 segundos");
    if (!a.lines.empty() && a.dark <= 0.0f)
      throw std::runtime_error("goto_scene: 'lines' necesita 'dark' (los segundos en negro)");
    result = a;
    ++found;
  }
  if (ja.contains("teleport")) {
    action::Teleport a;
    a.position = ReadVec3(ja, "teleport", {0.0f, 0.0f, 0.0f});
    if (ja.contains("yaw"))
      a.yaw = ja.at("yaw").get<float>();
    result = a;
    ++found;
  }
  if (ja.contains("set_flag")) {
    action::SetFlag a;
    a.name = ja.at("set_flag").get<std::string>();
    a.value = ja.value("value", 1);
    result = a;
    ++found;
  }
  if (ja.contains("add_flag")) {
    action::AddFlag a;
    a.name = ja.at("add_flag").get<std::string>();
    a.amount = ja.value("amount", 1);
    result = a;
    ++found;
  }
  if (ja.contains("message")) {
    action::Message a;
    a.text = ja.at("message").get<std::string>();
    a.seconds = ja.value("seconds", 3.0f);
    result = a;
    ++found;
  }
  if (ja.contains("play_sound")) {
    action::PlaySound a;
    a.file = ja.at("play_sound").get<std::string>();
    a.volume = ja.value("volume", 1.0f);
    a.radius = ja.value("radius", 14.0f);
    a.minDistance = ja.value("minDistance", 1.0f);
    if (ja.contains("at")) {
      if (ja.at("at").is_string()) {
        a.atEntity = ja.at("at").get<std::string>();
      } else {
        a.at = ReadVec3(ja, "at", {0.0f, 0.0f, 0.0f});
      }
    }
    if (a.radius <= 0.0f || a.minDistance < 0.0f || a.minDistance >= a.radius) {
      throw std::runtime_error("'radius' debe ser positivo y mayor que 'minDistance'");
    }
    result = a;
    ++found;
  }
  if (ja.contains("play_music")) {
    action::PlayMusic a;
    a.file = ja.at("play_music").get<std::string>();
    a.then = ja.value("then", std::string{});
    a.volume = ja.value("volume", 0.6f);
    a.fade = ja.value("fade", 1.0f);
    result = a;
    ++found;
  }
  if (ja.contains("stop_music")) {
    action::StopMusic a;
    a.fade = ja.value("fade", 1.0f);
    result = a;
    ++found;
  }
  // Destino de look_at y scare: el nombre de una entidad o un punto.
  auto readTarget = [&](const char* key, std::string& entity, std::optional<Vector3>& point) {
    if (ja.at(key).is_string())
      entity = ja.at(key).get<std::string>();
    else
      point = ReadVec3(ja, key, {0.0f, 0.0f, 0.0f});
  };
  if (ja.contains("look_at")) {
    if (ja.contains("fov"))
      throw std::runtime_error("'look_at' no acerca la vista: para eso esta 'scare'");
    action::LookAt a;
    readTarget("look_at", a.entity, a.point);
    a.speed = ja.value("speed", a.speed);
    if (a.speed <= 0.0f)
      throw std::runtime_error("'look_at': 'speed' debe ser positivo");
    result = a;
    ++found;
  }
  if (ja.contains("scare")) {
    action::Scare a;
    readTarget("scare", a.entity, a.point);
    a.speed = ja.value("speed", a.speed);
    a.fov = ja.value("fov", a.fov);
    if (a.speed <= 0.0f || a.fov < 1.0f || a.fov > 120.0f)
      throw std::runtime_error("'scare': 'speed' debe ser positivo y 'fov' de 1 a 120");
    result = a;
    ++found;
  }
  if (ja.contains("lock_movement")) {
    result = action::LockMovement{ja.at("lock_movement").get<bool>()};
    ++found;
  }
  if (ja.contains("title_screen")) {
    result = action::TitleScreen{};
    ++found;
  }
  if (ja.contains("ride")) {
    action::Ride a;
    a.entity = ja.at("ride").get<std::string>();
    a.seat = ReadVec3(ja, "seat", a.seat);
    if (ja.contains("exit"))
      a.exit = ReadVec3(ja, "exit", {0.0f, 0.0f, 0.0f});
    a.seconds = ja.value("seconds", a.seconds);
    a.flag = ja.value("flag", std::string{});
    a.after = ja.value("after", std::string{});
    a.locked = ja.value("locked", a.locked);
    if (a.entity.empty() || !ja.contains("seat") || a.seconds < 0.0f)
      throw std::runtime_error("'ride' necesita la entidad y 'seat'; 'seconds' no puede ser negativo");
    if (a.locked && a.seconds <= 0.0f)
      throw std::runtime_error("'ride' con 'locked' necesita 'seconds': si no, no se baja nunca");
    result = a;
    ++found;
  }
  if (ja.contains("behind_player")) {
    action::BehindPlayer a;
    a.entity = ja.at("behind_player").get<std::string>();
    a.distance = ja.value("distance", a.distance);
    a.side = ja.value("side", a.side);
    if (a.entity.empty() || a.distance <= 0.0f)
      throw std::runtime_error("'behind_player' necesita la entidad y una 'distance' positiva");
    result = a;
    ++found;
  }
  if (ja.contains("shooting")) {
    action::Shooting a;
    a.targets = ja.at("shooting").get<std::string>();
    a.position = ReadVec3(ja, "position", a.position);
    a.yaw = ja.value("yaw", a.yaw);
    a.seconds = ja.value("seconds", a.seconds);
    a.flag = ja.value("flag", std::string{});
    a.after = ja.value("after", std::string{});
    if (a.targets.empty() || !ja.contains("position") || a.seconds <= 0.0f || a.flag.empty())
      throw std::runtime_error("'shooting' necesita el prefijo de las dianas, 'position', 'flag' "
                               "y 'seconds' positivo");
    result = a;
    ++found;
  }
  if (ja.contains("dialogue")) {
    action::StartDialogue a;
    a.id = ja.at("dialogue").get<std::string>();
    if (a.id.empty())
      throw std::runtime_error("'dialogue' necesita el id de un dialogo");
    result = a;
    ++found;
  }

  if (found != 1) {
    throw std::runtime_error("cada accion debe tener exactamente una de: goto_scene, teleport, "
                             "set_flag, add_flag, message, play_sound, play_music, stop_music, "
                             "dialogue, look_at, scare, lock_movement, title_screen, ride, "
                             "shooting, behind_player");
  }
  return result;
}

namespace {

TriggerEvent ParseEvent(const std::string& s) {
  if (s == "enter")
    return TriggerEvent::Enter;
  if (s == "exit")
    return TriggerEvent::Exit;
  if (s == "stay")
    return TriggerEvent::Stay;

  throw std::runtime_error("evento desconocido: '" + s + "' (enter, exit o stay)");
}

// "interact": { "prompt": "Abrir", "range": 2.5, "once": true,
//               "box": { "min": [...], "max": [...] },     (opcional)
//               "if": ..., "do": [ ... ] }
// Sin "box": usa la caja de colision de la entidad si tiene UNA; si no (ninguna o varias), la
// caja de todo el modelo.
Interaction ReadInteraction(const json& ji, const BoxCollider& autoBox,
                            const std::vector<BoxCollider>& colliders) {
  Interaction it;
  it.prompt = ji.value("prompt", std::string("Interactuar"));
  it.range = ji.value("range", 2.5f);
  it.once = ji.value("once", false);
  it.conditions = ParseConditions(ji);

  for (const json& ja : ji.at("do"))
    it.actions.push_back(ParseAction(ja));
  if (it.actions.empty())
    throw std::runtime_error("'interact' necesita al menos una accion en 'do'");

  if (ji.contains("box")) {
    const json& jb = ji.at("box");
    it.box.min = ReadVec3(jb, "min", {0.0f, 0.0f, 0.0f});
    it.box.max = ReadVec3(jb, "max", {0.0f, 0.0f, 0.0f});
    if (it.box.min.x >= it.box.max.x || it.box.min.y >= it.box.max.y ||
        it.box.min.z >= it.box.max.z) {
      throw std::runtime_error(
          "'interact.box': cada componente de 'min' debe ser menor que el de 'max'");
    }
  } else if (colliders.size() == 1) {
    it.box = colliders.front();
  } else {
    it.box = autoBox;
  }
  return it;
}

void AddTrigger(const json& jt, Scene& scene) {
  Trigger t;
  t.name = jt.value("name", "trigger_" + std::to_string(scene.triggers.size()));
  EnsureUniqueTrigger(scene, t.name);

  t.box = ReadZone(jt);
  t.on = ParseEvent(jt.value("on", std::string("enter")));
  t.once = jt.value("once", false);
  t.marker = jt.value("marker", false);
  t.color = ReadColor(jt, "color", {255, 200, 80, 255});
  t.conditions = ParseConditions(jt);

  for (const json& ja : jt.at("do"))
    t.actions.push_back(ParseAction(ja));

  scene.triggers.push_back(std::move(t));
}

// "audio": { "music": "ruta" | { "file": "ruta", "volume": 0.5 },
//            "ambient": igual,
//            "footsteps": { "files": [ ... ], "volume": 0.6 },
//            "fade": 1.5 }
void ReadAudio(const json& ja, const std::string& root, Scene& scene) {
  auto checkFile = [&](const std::string& file) {
    if (!FileExists((root + file).c_str())) {
      throw std::runtime_error("archivo de audio inexistente: " + file);
    }
  };
  auto track = [&](const char* key, std::string& file, float& volume) {
    if (!ja.contains(key))
      return;
    const json& t = ja.at(key);
    if (t.is_string()) {
      file = t.get<std::string>();
    } else if (t.is_object()) {
      file = t.at("file").get<std::string>();
      volume = t.value("volume", volume);
    } else {
      throw std::runtime_error(std::string("'") + key + "' debe ser una ruta o { file, volume }");
    }
    checkFile(file);
  };

  SceneAudio& a = scene.audio;
  track("music", a.music, a.musicVolume);
  track("ambient", a.ambient, a.ambientVolume);

  if (ja.contains("footsteps")) {
    const json& f = ja.at("footsteps");
    for (const json& item : f.at("files")) {
      a.footsteps.push_back(item.get<std::string>());
      checkFile(a.footsteps.back());
    }
    a.footstepVolume = f.value("volume", a.footstepVolume);
  }
  a.fade = ja.value("fade", a.fade);
}

struct SoundSpec {
  std::string file;
  float volume;
  float radius;
  float minDistance;
};

// { "file": "...", "volume": 1.0, "radius": 10, "minDistance": 1 }
SoundSpec ReadSoundSpec(const json& j, const std::string& root) {
  SoundSpec s;
  s.file = j.at("file").get<std::string>();
  if (!FileExists((root + s.file).c_str())) {
    throw std::runtime_error("archivo de audio inexistente: " + s.file);
  }
  s.volume = j.value("volume", 1.0f);
  s.radius = j.value("radius", 10.0f);
  s.minDistance = j.value("minDistance", 1.0f);
  if (s.radius <= 0.0f || s.minDistance < 0.0f || s.minDistance >= s.radius) {
    throw std::runtime_error("'radius' debe ser positivo y mayor que 'minDistance'");
  }
  return s;
}

void AddEntity(const json& je, RetroEngine& engine, Scene& scene, bool autoCollide) {
  const std::string name = je.at("name").get<std::string>();
  if (scene.Find(name) != nullptr) {
    throw std::runtime_error("nombre de entidad duplicado: '" + name + "'");
  }

  const bool isSprite = je.contains("sprite");
  if (isSprite == je.contains("model")) {
    throw std::runtime_error("cada entidad necesita exactamente uno de: model, sprite");
  }

  std::unique_ptr<Entity> entity;
  BoxCollider autoBox;
  std::vector<BoxCollider> autoBoxes;
  bool isMeshFile = false;

  if (isSprite) {
    auto texture = engine.Assets().GetTexture(je.at("sprite").get<std::string>());
    texture->SetWrap(TEXTURE_WRAP_CLAMP);

    const Vector2 size = ReadVec2(je, "size");
    if (size.x <= 0.0f || size.y <= 0.0f)
      throw std::runtime_error("'size' debe ser positivo");

    autoBox = SpriteBox(size);
    autoBoxes = {autoBox};
    entity = std::make_unique<Entity>(name, std::move(texture), size);
  } else {
    auto model = LoadModelSpec(je.at("model"), engine);

    std::shared_ptr<TextureResource> texture;
    if (je.contains("texture")) {
      texture = engine.Assets().GetTexture(je.at("texture").get<std::string>());
      if (je.contains("wrap"))
        texture->SetWrap(ParseWrap(je.at("wrap").get<std::string>()));
    }

    autoBox = ComputeAutoBox(*model);
    autoBoxes = ModelColliders(*model, autoBox);
    isMeshFile = je.at("model").is_string();
    entity = std::make_unique<Entity>(name, std::move(model), std::move(texture));
  }
  entity->emissive = je.value("emissive", false);
  entity->fog = je.value("fog", true);

  std::vector<BoxCollider> colliders = ReadCollider(je, autoBoxes, isMeshFile, autoCollide);
  std::optional<Interaction> interact;
  if (je.contains("interact"))
    interact = ReadInteraction(je.at("interact"), autoBox, colliders);

  // "seen": { "range": 15, "angle": 20, "if": { ... }, "do": [ ... ], "away": [ ... ] }
  std::optional<Sight> sight;
  if (je.contains("seen")) {
    const json& js = je.at("seen");
    Sight s;
    s.range = js.value("range", s.range);
    s.angle = js.value("angle", s.angle);
    s.awayAngle = js.value("awayAngle", s.awayAngle);
    if (s.range <= 0.0f || s.angle <= 0.0f || s.angle >= 90.0f || s.awayAngle < s.angle ||
        s.awayAngle > 180.0f)
      throw std::runtime_error("'seen': 'range' positivo, 'angle' entre 0 y 90 y 'awayAngle' "
                               "entre 'angle' y 180");
    s.conditions = ParseConditions(js);
    for (const char* key : {"do", "away"}) {
      if (!js.contains(key))
        continue;
      for (const json& ja : js.at(key))
        (std::string(key) == "do" ? s.onSeen : s.onLookAway).push_back(ParseAction(ja));
    }
    sight = std::move(s);
  }

  Entity& e = scene.Add(std::move(entity));
  e.colliders = std::move(colliders);
  e.interact = std::move(interact);
  e.sight = std::move(sight);
  e.transform.position = ReadVec3(je, "position", {0.0f, 0.0f, 0.0f});
  e.transform.rotation = ReadVec3(je, "rotation", {0.0f, 0.0f, 0.0f});
  e.transform.scale = ReadVec3(je, "scale", {1.0f, 1.0f, 1.0f});
  e.tint = ReadColor(je, "tint", WHITE);
  if (je.contains("layer")) {
    if (e.IsSprite())
      throw std::runtime_error("'layer' solo vale para modelos");
    e.layer = je.at("layer").get<int>();
  }
  if (je.contains("frames")) {
    if (!e.IsSprite())
      throw std::runtime_error("'frames' solo vale para sprites");
    const int frames = je.at("frames").get<int>();
    const float fps = je.value("fps", 8.0f);
    if (frames < 1 || fps <= 0.0f)
      throw std::runtime_error("'frames' debe ser >= 1 y 'fps' positivo");
    // desfase segun la posicion: dos llamas no parpadean a la vez
    const Vector3 p = e.transform.position;
    const float h = std::sin(p.x * 12.9898f + p.y * 4.1414f + p.z * 78.233f) * 43758.5453f;
    e.SetAnimation(frames, fps, (h - std::floor(h)) * static_cast<float>(frames) / fps);
  }
  e.visible = je.value("visible", true);
  e.showIf = ParseConditions(je, "showIf");
  // "glitch": true (0.45 s) o los segundos: aparece y desaparece con interferencia
  if (je.contains("glitch")) {
    const json& jg = je.at("glitch");
    e.glitch = jg.is_boolean() ? (jg.get<bool>() ? 0.45f : 0.0f) : jg.get<float>();
    if (e.glitch < 0.0f || e.glitch > 5.0f)
      throw std::runtime_error("'glitch' debe ser true/false o de 0 a 5 segundos");
  }

  if (je.contains("sound")) {
    const json& js = je.at("sound");
    const SoundSpec spec = ReadSoundSpec(js, engine.Assets().Root());
    EntitySound es;
    es.offset = ReadVec3(js, "offset", {0.0f, 0.0f, 0.0f});
    es.file = spec.file;
    es.volume = spec.volume;
    es.radius = spec.radius;
    es.minDistance = spec.minDistance;
    e.sound = es;
  }

  if (je.contains("light")) {
    const json& jl = je.at("light");
    EntityLight el;
    el.offset = ReadVec3(jl, "offset", {0.00f, 0.0f, 0.0f});
    el.light = ReadPointLight(jl, {0.0f, 0.0f, 0.0f});
    el.light.phase = static_cast<float>(std::hash<std::string>{}(name) % 997u);
    e.light = el;
  }

  if (je.contains("facePlayer")) {
    const json& jf = je.at("facePlayer");
    if (jf.is_boolean()) {
      e.facePlayer = jf.get<bool>();
    } else if (jf.is_object()) {
      e.facePlayer = true;
      e.lookRange = jf.value("range", 10.0f);
      e.turnSpeed = jf.value("turnSpeed", 240.0f);
      if (e.lookRange <= 0.0f || e.turnSpeed <= 0.0f) {
        throw std::runtime_error("'facePlayer': 'range' y 'turnSpeed' deben ser positivos");
      }
    } else {
      throw std::runtime_error("'facePlayer' debe ser true o un objeto");
    }
  }

  if (je.contains("attach")) {
    const json& ja = je.at("attach");
    Entity::Attach at;
    at.to = ja.at("to").get<std::string>();
    if (at.to.empty() || at.to == name)
      throw std::runtime_error("'attach': 'to' debe ser otra entidad");
    at.position = e.transform.position; // lo del JSON es relativo a la otra
    at.yaw = e.transform.rotation.y;
    e.attach = at;
  }

  if (je.contains("smoke")) {
    const json& js = je.at("smoke");
    Entity::Smoke sm;
    sm.texture = engine.Assets().GetTexture(js.at("texture").get<std::string>());
    sm.offset = ReadVec3(js, "offset", sm.offset);
    sm.rate = js.value("rate", sm.rate);
    sm.life = js.value("life", sm.life);
    sm.rise = js.value("rise", sm.rise);
    if (js.contains("size"))
      sm.size = {js.at("size").at(0).get<float>(), js.at("size").at(1).get<float>()};
    if (js.contains("tint"))
      sm.tint = ReadColor(js, "tint", sm.tint);
    if (sm.rate <= 0.0f || sm.life <= 0.0f || sm.rate * sm.life > 200.0f)
      throw std::runtime_error("'smoke': 'rate' y 'life' positivos (y no mas de 200 bocanadas)");
    e.smoke = sm;
  }

  if (je.contains("slide")) {
    const json& js = je.at("slide");
    Entity::Slide sl;
    sl.offset = ReadVec3(js, "offset", sl.offset);
    sl.period = js.value("period", sl.period);
    sl.phase = js.value("phase", sl.phase);
    if (sl.period <= 0.0f)
      throw std::runtime_error("'slide': 'period' debe ser positivo");
    sl.base = e.transform.position;
    e.slide = sl;
  }

  if (je.contains("arrive")) {
    const json& ja = je.at("arrive");
    Entity::Arrive ar;
    ar.offset = ReadVec3(ja, "offset", ar.offset);
    ar.seconds = ja.value("seconds", ar.seconds);
    ar.brake = ja.value("brake", ar.brake);
    if (ar.seconds <= 0.0f || ar.brake <= 0.0f || ar.brake > 1.0f)
      throw std::runtime_error("'arrive': 'seconds' positivo y 'brake' entre 0 (sin incluir) y 1");
    ar.base = e.transform.position;
    e.arrive = ar;
  }

  if (je.contains("idle")) {
    const json& ji = je.at("idle");
    Entity::Idle idle;
    if (ji.is_object()) {
      idle.bob = ji.value("bob", idle.bob);
      idle.sway = ji.value("sway", idle.sway);
      idle.speed = ji.value("speed", idle.speed);
    } else if (!(ji.is_boolean() && ji.get<bool>())) {
      throw std::runtime_error("'idle' debe ser true o un objeto");
    }
    idle.phase = static_cast<float>(std::hash<std::string>{}(name) % 997u);
    e.idle = idle;
  }

  const Vector3 spin = ReadVec3(je, "spin", {0.0f, 0.0f, 0.0f});
  if (spin.x != 0.0f || spin.y != 0.0f || spin.z != 0.0f) {
    e.SetUpdate([spin](Entity& self, float dt) {
      self.transform.rotation = Vector3Add(self.transform.rotation, Vector3Scale(spin, dt));
    });
  }
}

} // namespace

void LoadSceneFromJson(const nlohmann::json& root, const std::string& relativePath,
                       RetroEngine& engine, Scene& scene, RetroCamera& camera) {
  scene.path = relativePath;
  std::string context = "interpretando JSON";
  try {
    if (root.contains("camera")) {
      context = "camara";
      const json& c = root.at("camera");
      camera.SetPosition(ReadVec3(c, "position", camera.Position()));
      camera.LookAt(ReadVec3(c, "target", camera.Target()));
      if (c.contains("fov"))
        camera.SetFov(c.at("fov").get<float>());
    }

    if (root.contains("background")) {
      context = "fondo";
      engine.SetClearColor(ReadColor(root, "background", engine.Config().clearColor));
    }

    if (root.contains("fog") || root.contains("light")) {
      RetroShaderParams p = engine.Retro().Params();

      if (root.contains("fog")) {
        context = "niebla";
        p.fogStart = root.at("fog").value("start", p.fogStart);
        p.fogEnd = root.at("fog").value("end", p.fogEnd);
        // neblina baja: "mist" (densidad 0-1), "mistHeight" (m) y "mistColor" [r,g,b]
        const json& jf = root.at("fog");
        p.mistDensity = jf.value("mist", p.mistDensity);
        p.mistHeight = jf.value("mistHeight", p.mistHeight);
        if (jf.contains("mistColor")) {
          const Color c = ReadColor(jf, "mistColor", WHITE);
          p.mistColor = {c.r / 255.0f, c.g / 255.0f, c.b / 255.0f};
        }
        if (p.mistDensity < 0.0f || p.mistDensity > 1.0f || p.mistHeight <= 0.0f)
          throw std::runtime_error("'mist' debe estar entre 0 y 1 y 'mistHeight' ser positivo");
      }
      if (root.contains("light")) {
        context = "luz";
        const json& l = root.at("light");
        p.lightDirection = ReadVec3(l, "direction", p.lightDirection);
        p.ambient = l.value("ambient", p.ambient);
        p.sun = l.value("sun", p.sun);
        if (p.sun < 0.0f || p.sun > 1.0f)
          throw std::runtime_error("'sun' debe estar entre 0 y 1");
      }
      engine.Retro().SetParams(p);
    }

    if (root.contains("player")) {
      context = "jugador";
      const json& p = root.at("player");
      scene.spawn.position = ReadVec3(p, "position", scene.spawn.position);
      scene.spawn.yaw = p.value("yaw", scene.spawn.yaw);
    }

    // "panorama": { "model": "models/x.obj", "tint": [r,g,b], "haze": 0.4, "y": -6,
    //               "sun": { "texture": "textures/spr_sol.png", "yaw": 0, "pitch": 8, "size": 40 } }
    // yaw/pitch del sol: como la vista del jugador (yaw 0 = hacia -Z; pitch, grados sobre el
    // horizonte).
    if (root.contains("panorama")) {
      context = "panorama";
      const json& jp = root.at("panorama");
      Scene::Panorama pano;
      pano.model = engine.Assets().GetModel(jp.at("model").get<std::string>());
      pano.model->SetShader(engine.Retro().Lit());
      pano.tint = ReadColor(jp, "tint", WHITE);
      pano.haze = jp.value("haze", pano.haze);
      pano.y = jp.value("y", pano.y);
      if (pano.haze < 0.0f || pano.haze > 1.0f)
        throw std::runtime_error("'haze' debe estar entre 0 y 1");
      if (jp.contains("sun")) {
        const json& js = jp.at("sun");
        pano.sun = engine.Assets().GetTexture(js.at("texture").get<std::string>());
        const float yaw = js.value("yaw", 0.0f) * DEG2RAD;
        const float pitch = js.value("pitch", 8.0f) * DEG2RAD;
        pano.sunDirection = {-std::sin(yaw) * std::cos(pitch), std::sin(pitch),
                             -std::cos(yaw) * std::cos(pitch)};
        pano.sunSize = js.value("size", pano.sunSize);
      }
      scene.panorama = std::move(pano);
    }

    if (root.contains("bounds")) {
      context = "bounds";
      const json& jb = root.at("bounds");
      Scene::Bounds b;
      b.min = ReadVec2(jb, "min");
      b.max = ReadVec2(jb, "max");
      if (b.min.x >= b.max.x || b.min.y >= b.max.y) {
        throw std::runtime_error("cada componente de 'min' debe ser menor que el de 'max'");
      }

      const Vector3& s = scene.spawn.position;
      if (s.x < b.min.x || s.x > b.max.x || s.z < b.min.y || s.z > b.max.y) {
        throw std::runtime_error("'player.position' esta fuera de 'bounds'");
      }

      scene.bounds = b;
    }

    const bool autoCollide = root.value("autoCollide", false);

    if (root.contains("audio")) {
      context = "audio";
      ReadAudio(root.at("audio"), engine.Assets().Root(), scene);
    }

    if (root.contains("lights")) {
      int lightIndex = 0;
      for (const json& jl : root.at("lights")) {
        context = "luz #" + std::to_string(lightIndex);
        PointLight l = ReadPointLight(jl, ReadVec3(jl, "position", {0.0f, 0.0f, 0.0f}));
        l.phase = static_cast<float>(lightIndex++);
        scene.lights.push_back(l);
      }
    }

    if (root.contains("emitters")) {
      int emitterIndex = 0;
      for (const json& je : root.at("emitters")) {
        context = "emisor #" + std::to_string(emitterIndex);
        const SoundSpec spec = ReadSoundSpec(je, engine.Assets().Root());
        Emitter em;
        em.key = "scene:" + std::to_string(emitterIndex++);
        em.file = spec.file;
        em.position = ReadVec3(je, "position", {0.0f, 0.0f, 0.0f});
        em.volume = spec.volume;
        em.radius = spec.radius;
        em.minDistance = spec.minDistance;
        scene.emitters.push_back(em);
      }
    }

    int index = 0;
    for (const json& je : root.at("entities")) {
      context = "entidad #" + std::to_string(index++);
      if (je.contains("name") && je["name"].is_string()) {
        context += " '" + je["name"].get<std::string>() + "'";
      }
      AddEntity(je, engine, scene, autoCollide);
    }

    if (root.contains("triggers")) {
      int triggerIndex = 0;
      for (const json& jt : root.at("triggers")) {
        context = "trigger #" + std::to_string(triggerIndex++);
        if (jt.contains("name") && jt["name"].is_string()) {
          context += " '" + jt["name"].get<std::string>() + "'";
        }
        AddTrigger(jt, scene);
      }
    }

  } catch (const std::exception& e) {
    throw std::runtime_error(relativePath + " (" + context + "): " + e.what());
  }
}

void LoadScene(const std::string& relativePath, RetroEngine& engine, Scene& scene,
               RetroCamera& camera) {
  const std::string fullPath = engine.Assets().Root() + relativePath;

  std::ifstream file(fullPath);
  if (!file)
    throw std::runtime_error("No se pudo abrir la escena: " + fullPath);

  json root;
  try {
    root = json::parse(file);
  } catch (const std::exception& e) {
    throw std::runtime_error(relativePath + " (parseando JSON): " + e.what());
  }
  LoadSceneFromJson(root, relativePath, engine, scene, camera);
}

} // namespace retro