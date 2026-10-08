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

#include <fstream>
#include <memory>
#include <optional>
#include <stdexcept>
#include <string>

namespace retro {

namespace {

using json = nlohmann::json;

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

// "collider": true | false | "auto" | { "min": [x,y,z], "max": [x,y,z] }
// Sin el campo: caja automática solo si la escena tiene autoCollide y el
// modelo es un archivo .obj (los planos procedurales usan el suelo Y=0).
std::optional<BoxCollider> ReadCollider(const json& je, ModelResource& model, bool isMeshFile,
                                        bool autoCollide) {
  if (je.contains("collider")) {
    const json& jc = je.at("collider");

    if (jc.is_boolean()) {
      if (!jc.get<bool>())
        return std::nullopt;
      return ComputeAutoBox(model);
    }
    if (jc.is_string() && jc.get<std::string>() == "auto")
      return ComputeAutoBox(model);

    if (jc.is_object()) {
      if (!jc.contains("min") || !jc.contains("max")) {
        throw std::runtime_error("'collider' necesita 'min' y 'max'");
      }
      BoxCollider box;
      box.min = ReadVec3(jc, "min", {0.0f, 0.0f, 0.0f});
      box.max = ReadVec3(jc, "max", {0.0f, 0.0f, 0.0f});
      if (box.min.x >= box.max.x || box.min.y >= box.max.y || box.min.z >= box.max.z) {
        throw std::runtime_error(
            "'collider': cada componente de 'min' debe ser menor que el de 'max'");
      }
      return box;
    }
    throw std::runtime_error("'collider' debe ser true, false, \"auto\" o {min, max}");
  }

  if (autoCollide && isMeshFile)
    return ComputeAutoBox(model);
  return std::nullopt;
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

Condition ParseCondition(const json& jc) {
  Condition c;
  c.flag = jc.at("flag").get<std::string>();
  c.equals = jc.value("equals", 1);
  c.negate = jc.value("not", false);
  return c;
}

std::vector<Condition> ParseConditions(const json& jt) {
  std::vector<Condition> out;
  if (!jt.contains("if"))
    return out;

  const json& jc = jt.at("if");
  if (jc.is_object()) {
    out.push_back(ParseCondition(jc));
  } else if (jc.is_array()) {
    for (const json& item : jc)
      out.push_back(ParseCondition(item));
  } else {
    throw std::runtime_error("'if' debe ser un objeto o un arreglo de objetos");
  }

  return out;
}

// Cada accion es un objeto con UNA clave de accion y, si hace falta, modificadores:
//   { "goto_scene": "scenes/x.json", "position": [x,y,z], "yaw": 0 }
//   { "teleport": [x,y,z], "yaw": 90 }
//   { "set_flag": "nombre", "value": 1 }
//   { "message": "texto\notra linea", "seconds": 3 }
//   { "set_visible": "entidad", "visible": false }
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
  if (ja.contains("message")) {
    action::Message a;
    a.text = ja.at("message").get<std::string>();
    a.seconds = ja.value("seconds", 3.0f);
    result = a;
    ++found;
  }
  if (ja.contains("set_visible")) {
    action::SetVisible a;
    a.entity = ja.at("set_visible").get<std::string>();
    a.visible = ja.value("visible", true);
    result = a;
    ++found;
  }

  if (found != 1) {
    throw std::runtime_error(
        "cada accion debe tener exactamente una de: goto_scene, teleport, set_flag, "
        "message, set_visible");
  }
  return result;
}

TriggerEvent ParseEvent(const std::string& s) {
  if (s == "enter")
    return TriggerEvent::Enter;
  if (s == "exit")
    return TriggerEvent::Exit;
  if (s == "stay")
    return TriggerEvent::Stay;

  throw std::runtime_error("evento desconocido: '" + s + "' (enter, exit o stay)");
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

void AddPortal(const json& jp, Scene& scene) {
  Trigger t;
  t.name = jp.value("name", "portal_" + std::to_string(scene.triggers.size()));
  EnsureUniqueTrigger(scene, t.name);

  const json& to = jp.at("to");
  action::GotoScene g;
  g.scene = to.at("scene").get<std::string>();
  if (to.contains("position"))
    g.position = ReadVec3(to, "position", {0.0f, 0.0f, 0.0f});
  if (to.contains("yaw"))
    g.yaw = to.at("yaw").get<float>();

  t.box = ReadZone(jp);
  t.marker = true;
  t.color = ReadColor(jp, "color", {255, 200, 80, 255});
  t.actions.push_back(std::move(g));

  scene.triggers.push_back(std::move(t));
}

void AddEntity(const json& je, RetroEngine& engine, Scene& scene, bool autoCollide) {
  const std::string name = je.at("name").get<std::string>();
  if (scene.Find(name) != nullptr) {
    throw std::runtime_error("nombre de entidad duplicado: '" + name + "'");
  }

  auto model = LoadModelSpec(je.at("model"), engine);

  std::shared_ptr<TextureResource> texture;
  if (je.contains("texture")) {
    texture = engine.Assets().GetTexture(je.at("texture").get<std::string>());
    if (je.contains("wrap"))
      texture->SetWrap(ParseWrap(je.at("wrap").get<std::string>()));
  }

  // Antes de añadir la entidad: si el collider es inválido, no queda a medias.
  const std::optional<BoxCollider> collider =
      ReadCollider(je, *model, je.at("model").is_string(), autoCollide);

  Entity& e = scene.Add(std::make_unique<Entity>(name, model, std::move(texture)));
  e.collider = collider;
  e.transform.position = ReadVec3(je, "position", {0.0f, 0.0f, 0.0f});
  e.transform.rotation = ReadVec3(je, "rotation", {0.0f, 0.0f, 0.0f});
  e.transform.scale = ReadVec3(je, "scale", {1.0f, 1.0f, 1.0f});
  e.tint = ReadColor(je, "tint", WHITE);
  e.visible = je.value("visible", true);

  const Vector3 spin = ReadVec3(je, "spin", {0.0f, 0.0f, 0.0f});
  if (spin.x != 0.0f || spin.y != 0.0f || spin.z != 0.0f) {
    e.SetUpdate([spin](Entity& self, float dt) {
      self.transform.rotation = Vector3Add(self.transform.rotation, Vector3Scale(spin, dt));
    });
  }
}

} // namespace

void LoadScene(const std::string& relativePath, RetroEngine& engine, Scene& scene,
               RetroCamera& camera) {
  const std::string fullPath = engine.Assets().Root() + relativePath;

  std::ifstream file(fullPath);
  if (!file)
    throw std::runtime_error("No se pudo abrir la escena: " + fullPath);

  scene.path = relativePath;
  std::string context = "parseando JSON";
  try {
    const json root = json::parse(file);

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
      }
      if (root.contains("light")) {
        context = "luz";
        const json& l = root.at("light");
        p.lightDirection = ReadVec3(l, "direction", p.lightDirection);
        p.ambient = l.value("ambient", p.ambient);
      }
      engine.Retro().SetParams(p);
    }

    if (root.contains("player")) {
      context = "jugador";
      const json& p = root.at("player");
      scene.spawn.position = ReadVec3(p, "position", scene.spawn.position);
      scene.spawn.yaw = p.value("yaw", scene.spawn.yaw);
    }
    const bool autoCollide = root.value("autoCollide", false);

    int index = 0;
    for (const json& je : root.at("entities")) {
      context = "entidad #" + std::to_string(index++);
      if (je.contains("name") && je["name"].is_string()) {
        context += " '" + je["name"].get<std::string>() + "'";
      }
      AddEntity(je, engine, scene, autoCollide);
    }

    if (root.contains("portals")) {
      int portalIndex = 0;
      for (const json& jp : root.at("portals")) {
        context = "portal #" + std::to_string(portalIndex++);
        AddPortal(jp, scene);
      }
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

} // namespace retro