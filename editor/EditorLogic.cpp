#include "EditorApp.hpp"

#include <array>
#include <cstddef>
#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace editor {

namespace {

struct ActionInfo {
  const char* key;
  const char* title;
};

constexpr std::array<ActionInfo, 8> kActions{{
    {"message", "Mensaje"},
    {"set_flag", "Fijar bandera"},
    {"add_flag", "Sumar a bandera"},
    {"goto_scene", "Cambiar de escena"},
    {"teleport", "Teletransportar"},
    {"play_sound", "Sonido"},
    {"play_music", "Musica"},
    {"stop_music", "Parar musica"},
}};

const ActionInfo* FindAction(const Json& action) {
  if (!action.is_object())
    return nullptr;
  for (const ActionInfo& info : kActions) {
    if (action.contains(info.key))
      return &info;
  }
  return nullptr;
}

std::string ActionSummary(const Json& action) {
  const ActionInfo* info = FindAction(action);
  if (!info)
    return "(accion desconocida)";

  std::string text = info->title;
  const Json& value = action.at(info->key);
  if (value.is_string()) {
    std::string s = value.get<std::string>();
    for (char& c : s) {
      if (c == '\n')
        c = ' ';
    }
    if (s.size() > 32)
      s = s.substr(0, 29) + "...";
    text += ": " + s;
  }
  return text;
}

Json NewAction(const std::string& key, const std::vector<std::string>& scenes,
               const std::vector<std::string>& sounds) {
  const std::string firstScene = scenes.empty() ? std::string() : scenes.front();
  const std::string firstSound = sounds.empty() ? std::string() : sounds.front();

  if (key == "message")
    return Json::object({{"message", "Texto"}});
  if (key == "set_flag")
    return Json::object({{"set_flag", "bandera"}});
  if (key == "add_flag")
    return Json::object({{"add_flag", "bandera"}});
  if (key == "goto_scene")
    return Json::object({{"goto_scene", firstScene}});
  if (key == "teleport")
    return Json::object({{"teleport", Vec3Json({0.0f, 0.0f, 0.0f})}});
  if (key == "play_sound")
    return Json::object({{"play_sound", firstSound}});
  if (key == "play_music")
    return Json::object({{"play_music", firstSound}});
  return Json::object({{"stop_music", true}});
}

} // namespace

// ---------------------------------------------------------------------------
// Listas: botones comunes a acciones y condiciones
// ---------------------------------------------------------------------------
EditorApp::ListOp EditorApp::ListButtons(const ListSlot& slot) {
  ListOp op = ListOp::None;

  ImGui::BeginDisabled(slot.index == 0);
  if (ImGui::SmallButton("Subir"))
    op = ListOp::Up;
  ImGui::EndDisabled();
  ImGui::SameLine();

  ImGui::BeginDisabled(slot.index + 1 >= slot.count);
  if (ImGui::SmallButton("Bajar"))
    op = ListOp::Down;
  ImGui::EndDisabled();
  ImGui::SameLine();

  ImGui::BeginDisabled(!slot.canRemove);
  if (ImGui::SmallButton("Quitar"))
    op = ListOp::Remove;
  ImGui::EndDisabled();
  return op;
}

void EditorApp::ApplyListOp(Json& list, ListOp op, std::size_t index) {
  if (op == ListOp::None || !list.is_array() || index >= list.size())
    return;
  if (op == ListOp::Up && index == 0)
    return;
  if (op == ListOp::Down && index + 1 >= list.size())
    return;

  BeginChange(0);
  if (op == ListOp::Remove) {
    list.erase(list.begin() + static_cast<std::ptrdiff_t>(index));
  } else {
    const std::size_t other = op == ListOp::Up ? index - 1 : index + 1;
    std::swap(list[index], list[other]);
  }
  MarkChanged(0);
}

std::vector<std::string> EditorApp::EntityNames() const {
  std::vector<std::string> names;
  if (!doc_.is_object() || !doc_.contains("entities") || !doc_.at("entities").is_array())
    return names;
  for (const Json& e : doc_.at("entities")) {
    std::string name = ReadString(&e, "name");
    if (!name.empty())
      names.push_back(std::move(name));
  }
  return names;
}

void EditorApp::EditActionList(Json& owner, const char* key, bool keepOne) {
  if (!owner.contains(key) || !owner.at(key).is_array()) {
    ImGui::TextDisabled("'%s' no es una lista de acciones", key);
    return;
  }
  Json& list = owner[key];

  // Borrar o mover cambia la lista: se apunta aqui y se hace DESPUES del bucle, porque hacerlo
  // dentro dejaria colgando las referencias a los elementos que el bucle todavia usa.
  ListOp op = ListOp::None;
  std::size_t target = 0;

  if (list.empty())
    ImGui::TextDisabled("(sin acciones)");

  for (std::size_t i = 0; i < list.size(); ++i) {
    ImGui::PushID(static_cast<int>(i));

    // "###accion": el ID no depende del texto, asi el nodo no se cierra al cambiar el resumen.
    const std::string header = std::to_string(i + 1) + ". " + ActionSummary(list[i]) + "###accion";
    if (ImGui::TreeNodeEx(header.c_str(), ImGuiTreeNodeFlags_DefaultOpen)) {
      const ListOp asked = ListButtons({i, list.size(), !(keepOne && list.size() == 1)});
      if (asked != ListOp::None) {
        op = asked;
        target = i;
      }
      EditAction(list[i]);
      ImGui::TreePop();
    }
    ImGui::PopID();
  }
  ApplyListOp(list, op, target);

  if (ImGui::BeginCombo("##nueva_accion", "+ Anadir accion")) {
    for (const ActionInfo& info : kActions) {
      if (ImGui::Selectable(info.title)) {
        BeginChange(0);
        list.push_back(NewAction(info.key, sceneList_, assets_.sounds));
        MarkChanged(0);
      }
    }
    ImGui::EndCombo();
  }
}

void EditorApp::EditAction(Json& a) {
  const ActionInfo* info = FindAction(a);
  if (!info) {
    ImGui::TextDisabled("Accion no reconocida (solo lectura):");
    ImGui::TextWrapped("%s", a.dump().c_str());
    return;
  }
  const std::string key = info->key;

  if (key == "message") {
    FieldText("Texto", a, nullptr, "message", true);
    ImGui::TextDisabled("{bandera} muestra el valor de esa bandera");
    FieldFloat("Segundos", a, nullptr, "seconds", 3.0f, 0.1f, 0.5f, 60.0f);

  } else if (key == "set_flag") {
    FieldText("Bandera", a, nullptr, "set_flag");
    FieldInt("Valor", a, nullptr, "value", 1, -9999, 9999);

  } else if (key == "add_flag") {
    FieldText("Bandera", a, nullptr, "add_flag");
    FieldInt("Cantidad", a, nullptr, "amount", 1, -9999, 9999);

  } else if (key == "goto_scene") {
    FieldFile("Escena", a, nullptr, "goto_scene", sceneList_, false, false);
    bool arrival = a.contains("position");
    if (ImGui::Checkbox("Elegir punto de llegada", &arrival)) {
      BeginChange(0);
      if (arrival) {
        a["position"] = Vec3Json({0.0f, 0.0f, 0.0f});
        a["yaw"] = 0;
      } else {
        a.erase("position");
        a.erase("yaw");
      }
      MarkChanged(0);
    }
    if (a.contains("position")) {
      FieldVec3("Llegada", a, nullptr, "position", {0.0f, 0.0f, 0.0f}, 0.05f, false);
      FieldFloat("Giro al llegar", a, nullptr, "yaw", 0.0f, 0.5f, -360.0f, 360.0f, false);
    } else {
      ImGui::TextDisabled("Se aparece en el inicio de la escena de destino");
    }

  } else if (key == "teleport") {
    FieldVec3("Destino", a, nullptr, "teleport", {0.0f, 0.0f, 0.0f}, 0.05f, false);
    bool turn = a.contains("yaw");
    if (ImGui::Checkbox("Cambiar el giro", &turn)) {
      BeginChange(0);
      if (turn)
        a["yaw"] = 0;
      else
        a.erase("yaw");
      MarkChanged(0);
    }
    if (a.contains("yaw"))
      FieldFloat("Giro", a, nullptr, "yaw", 0.0f, 0.5f, -360.0f, 360.0f, false);

  } else if (key == "play_sound") {
    FieldFile("Archivo", a, nullptr, "play_sound", assets_.sounds, false, false);
    FieldFloat("Volumen", a, nullptr, "volume", 1.0f, 0.01f, 0.0f, 1.0f);

    // Donde suena: sin "at" (en la cabeza), "at": [x,y,z] o "at": "nombre_de_entidad".
    const char* modes[] = {"En la cabeza", "En un punto", "En una entidad"};
    int mode = !a.contains("at") ? 0 : (a.at("at").is_string() ? 2 : 1);
    int next = mode;
    if (ImGui::Combo("Donde", &next, modes, 3) && next != mode) {
      BeginChange(0);
      if (next == 0) {
        a.erase("at");
      } else if (next == 1) {
        a["at"] = Vec3Json({0.0f, 1.0f, 0.0f});
      } else {
        const std::vector<std::string> names = EntityNames();
        a["at"] = names.empty() ? std::string() : names.front();
      }
      MarkChanged(0);
      mode = next;
    }
    if (mode == 1)
      FieldVec3("Punto", a, nullptr, "at", {0.0f, 0.0f, 0.0f}, 0.05f, false);
    if (mode == 2)
      FieldFile("Entidad", a, nullptr, "at", EntityNames(), false, false);
    if (mode != 0) {
      FieldFloat("Radio", a, nullptr, "radius", 14.0f, 0.1f, 0.1f, 200.0f);
      FieldFloat("Distancia minima", a, nullptr, "minDistance", 1.0f, 0.05f, 0.0f, 100.0f);
    }

  } else if (key == "play_music") {
    FieldFile("Archivo", a, nullptr, "play_music", assets_.sounds, false, false);
    FieldFloat("Volumen", a, nullptr, "volume", 0.6f, 0.01f, 0.0f, 1.0f);
    FieldFloat("Fundido (s)", a, nullptr, "fade", 1.0f, 0.05f, 0.0f, 30.0f);

  } else if (key == "stop_music") {
    FieldFloat("Fundido (s)", a, nullptr, "fade", 1.0f, 0.05f, 0.0f, 30.0f);
  }
}

// ---------------------------------------------------------------------------
// Condiciones
// ---------------------------------------------------------------------------
// Un segundo espacio anonimo en el mismo archivo es el MISMO espacio: solo se reabre.
namespace {

// Los valores coinciden con las opciones del desplegable "Tipo" (Unknown queda fuera).
enum class CondKind { Compare = 0, All = 1, Any = 2, Not = 3, Unknown = 4 };

// El cargador acepta como mucho 8 niveles de anidacion (ParseCondition en SceneLoader.cpp).
constexpr int kMaxConditionDepth = 8;

const std::vector<std::string> kCompareOps = {"==", "!=", "<", "<=", ">", ">="};

// Que tipo de condicion es, con las mismas reglas que el cargador: exactamente una de las
// claves flag, all, any, not; y "all"/"any" deben ser arreglos.
CondKind KindOf(const Json& c) {
  if (!c.is_object())
    return CondKind::Unknown;
  const int found = static_cast<int>(c.contains("flag")) + static_cast<int>(c.contains("all")) +
                    static_cast<int>(c.contains("any")) + static_cast<int>(c.contains("not"));
  if (found != 1)
    return CondKind::Unknown;
  if (c.contains("flag"))
    return CondKind::Compare;
  if (c.contains("all"))
    return c.at("all").is_array() ? CondKind::All : CondKind::Unknown;
  if (c.contains("any"))
    return c.at("any").is_array() ? CondKind::Any : CondKind::Unknown;
  return CondKind::Not;
}

const char* GroupKey(CondKind kind) { return kind == CondKind::All ? "all" : "any"; }

Json NewCompare() { return Json::object({{"flag", "bandera"}}); }

Json NewCondition(CondKind kind) {
  switch (kind) {
  case CondKind::All:
    return Json::object({{"all", Json::array({NewCompare()})}});
  case CondKind::Any:
    return Json::object({{"any", Json::array({NewCompare()})}});
  case CondKind::Not:
    return Json::object({{"not", NewCompare()}});
  default:
    return NewCompare();
  }
}

// Cambia el tipo de una condicion conservando lo que se pueda: un grupo pasa de "todas" a
// "alguna" con los mismos hijos, una comparacion se envuelve en el grupo o en el "no", etc.
Json ConvertCondition(const Json& c, CondKind to) {
  const CondKind from = KindOf(c);
  if (from == to)
    return c;

  if (to == CondKind::Compare)
    return NewCompare();

  if (to == CondKind::All || to == CondKind::Any) {
    if (from == CondKind::All || from == CondKind::Any)
      return Json::object({{GroupKey(to), c.at(GroupKey(from))}});
    if (from == CondKind::Not)
      return Json::object({{GroupKey(to), Json::array({c.at("not")})}});
    return Json::object({{GroupKey(to), Json::array({c})}});
  }

  // A "no": un grupo se queda con su primer hijo; una comparacion se envuelve.
  if (from == CondKind::All || from == CondKind::Any) {
    const Json& children = c.at(GroupKey(from));
    return Json::object({{"not", children.empty() ? NewCompare() : children.front()}});
  }
  return Json::object({{"not", c}});
}

// Texto de una linea: "monedas >= 3", "Todas (2)", "NO tiene_llave == 1".
std::string ConditionSummary(const Json& c) {
  switch (KindOf(c)) {
  case CondKind::Compare: {
    const std::string op = ReadString(&c, "op");
    const int value = static_cast<int>(ReadNumber(&c, "value", 1.0f));
    return ReadString(&c, "flag") + " " + (op.empty() ? "==" : op) + " " + std::to_string(value);
  }
  case CondKind::All:
    return "Todas se cumplen (" + std::to_string(c.at("all").size()) + ")";
  case CondKind::Any:
    return "Alguna se cumple (" + std::to_string(c.at("any").size()) + ")";
  case CondKind::Not:
    return "NO " + ConditionSummary(c.at("not"));
  case CondKind::Unknown:
    break;
  }
  return "(condicion no reconocida)";
}

// Desplegable "+ Anadir condicion". Devuelve true y deja la nueva en 'out' si se eligio una.
// En el ultimo nivel permitido solo se ofrecen comparaciones (los grupos tendrian hijos de mas).
bool AddConditionCombo(const char* id, int depth, Json& out) {
  const char* kinds[] = {"Bandera", "Todas se cumplen (Y)", "Alguna se cumple (O)", "No se cumple"};
  const int choices = depth < kMaxConditionDepth ? 4 : 1;

  bool added = false;
  if (ImGui::BeginCombo(id, "+ Anadir condicion")) {
    for (int k = 0; k < choices; ++k) {
      if (ImGui::Selectable(kinds[k])) {
        out = NewCondition(static_cast<CondKind>(k));
        added = true;
      }
    }
    ImGui::EndCombo();
  }
  return added;
}

std::string Shorten(std::string s, std::size_t max) {
  if (s.size() > max)
    s = s.substr(0, max - 3) + "...";
  return s;
}

} // namespace

void EditorApp::EditConditions(Json& owner, const char* key) {
  if (!owner.contains(key)) {
    ImGui::TextDisabled("(sin condiciones: siempre se cumple)");
  } else if (owner.at(key).is_object()) {
    // Una sola condicion, sin lista. Quitarla deja el campo sin condiciones.
    const ListSlot slot{0, 1, true};
    if (EditCondition(owner[key], 0, &slot) == ListOp::Remove) {
      BeginChange(0);
      owner.erase(key);
      MarkChanged(0);
    }
  } else if (owner.at(key).is_array()) {
    ImGui::TextDisabled("Deben cumplirse todas:");
    EditConditionList(owner[key], 0, false);
    if (owner.at(key).empty()) {
      BeginChange(0);
      owner.erase(key);
      MarkChanged(0);
    }
  } else {
    ImGui::TextDisabled("'%s' no tiene un formato valido (solo lectura)", key);
    return;
  }

  // Anadir: sin condiciones se pone la nueva sola; con una, se pasa a lista de dos.
  Json added;
  if (AddConditionCombo("##nueva_condicion", 0, added)) {
    BeginChange(0);
    if (!owner.contains(key)) {
      owner[key] = std::move(added);
    } else if (owner.at(key).is_object()) {
      Json previous = std::move(owner[key]);
      owner[key] = Json::array({std::move(previous), std::move(added)});
    } else {
      owner[key].push_back(std::move(added));
    }
    MarkChanged(0);
  }
}

void EditorApp::EditConditionList(Json& list, int depth, bool keepOne) {
  ListOp op = ListOp::None;
  std::size_t target = 0;

  for (std::size_t i = 0; i < list.size(); ++i) {
    ImGui::PushID(static_cast<int>(i));
    // keepOne: un grupo "todas"/"alguna" no puede quedarse vacio (el cargador lo rechaza).
    const ListSlot slot{i, list.size(), !(keepOne && list.size() == 1)};
    const ListOp asked = EditCondition(list[i], depth, &slot);
    if (asked != ListOp::None) {
      op = asked;
      target = i;
    }
    ImGui::PopID();
  }
  ApplyListOp(list, op, target);
}

EditorApp::ListOp EditorApp::EditCondition(Json& c, int depth, const ListSlot* slot) {
  const CondKind kind = KindOf(c);
  const std::string header = Shorten(ConditionSummary(c), 48) + "###condicion";
  if (!ImGui::TreeNodeEx(header.c_str(), ImGuiTreeNodeFlags_DefaultOpen))
    return ListOp::None;

  const ListOp op = slot ? ListButtons(*slot) : ListOp::None;

  if (kind == CondKind::Unknown) {
    ImGui::TextDisabled("No reconocida (solo lectura):");
    ImGui::TextWrapped("%s", c.dump().c_str());
    ImGui::TreePop();
    return op;
  }

  // El cambio de tipo reemplaza 'c' entero: se aplica al final, cuando ya nadie usa sus hijos.
  const char* kinds[] = {"Bandera", "Todas se cumplen (Y)", "Alguna se cumple (O)", "No se cumple"};
  int next = static_cast<int>(kind);
  const int choices = depth < kMaxConditionDepth ? 4 : 1;
  std::optional<Json> replacement;
  if (ImGui::Combo("Tipo", &next, kinds, choices) && next != static_cast<int>(kind))
    replacement = ConvertCondition(c, static_cast<CondKind>(next));

  switch (kind) {
  case CondKind::Compare:
    FieldText("Bandera", c, nullptr, "flag");
    FieldChoice("Comparacion", c, nullptr, "op", kCompareOps, "==");
    FieldInt("Valor", c, nullptr, "value", 1, -9999, 9999);
    ImGui::TextDisabled("Una bandera que nunca se fijo vale 0");
    break;

  case CondKind::All:
  case CondKind::Any: {
    Json& children = c[GroupKey(kind)];
    EditConditionList(children, depth + 1, true);
    Json added;
    if (AddConditionCombo("##anadir_hijo", depth + 1, added)) {
      BeginChange(0);
      children.push_back(std::move(added));
      MarkChanged(0);
    }
    break;
  }

  case CondKind::Not:
    EditCondition(c["not"], depth + 1, nullptr);
    break;

  case CondKind::Unknown:
    break;
  }

  if (replacement) {
    BeginChange(0);
    c = std::move(*replacement);
    MarkChanged(0);
  }

  ImGui::TreePop();
  return op;
}

// ---------------------------------------------------------------------------
// Interaccion (tecla E)
// ---------------------------------------------------------------------------
void EditorApp::EditInteraction(Json& e, int entityIndex) {
  if (!e.contains("interact") || !e.at("interact").is_object()) {
    if (ImGui::Button("Anadir interaccion")) {
      BeginChange(0);
      // El cargador exige al menos una accion en "do": se empieza con un mensaje.
      e["interact"] = Json::object(
          {{"prompt", "Interactuar"}, {"do", Json::array({NewAction("message", {}, {})})}});
      MarkChanged(0);
    }
    return;
  }

  Json& it = e["interact"];
  ImGui::PushID("interact");

  FieldText("Texto (E: ...)", it, nullptr, "prompt");
  FieldFloat("Alcance", it, nullptr, "range", 2.5f, 0.05f, 0.1f, 50.0f);
  FieldBool("Una sola vez", it, nullptr, "once", false);

  // Caja propia: por defecto se usa la de colision o, si no hay, la del modelo o el sprite.
  bool ownBox = it.contains("box");
  if (ImGui::Checkbox("Caja propia", &ownBox)) {
    BeginChange(0);
    if (ownBox) {
      BoundingBox b{{-0.5f, 0.0f, -0.5f}, {0.5f, 1.0f, 0.5f}};
      if (scene_ && entityIndex >= 0 && static_cast<std::size_t>(entityIndex) < scene_->Count())
        b = scene_->EntityAt(static_cast<std::size_t>(entityIndex)).LocalBounds();
      it["box"] = Json::object({{"min", Vec3Json(b.min)}, {"max", Vec3Json(b.max)}});
    } else {
      it.erase("box");
    }
    MarkChanged(0);
  }
  if (it.contains("box")) {
    FieldVec3("Caja: min", it, "box", "min", {-0.5f, 0.0f, -0.5f}, 0.05f, false);
    FieldVec3("Caja: max", it, "box", "max", {0.5f, 1.0f, 0.5f}, 0.05f, false);
    ImGui::TextDisabled("En el espacio de la entidad (antes de escalar y girar)");
  } else {
    ImGui::TextDisabled("Usa la caja de colision, o la del modelo si no tiene");
  }

  ImGui::SeparatorText("Condiciones de la interaccion");
  ImGui::PushID("if");
  EditConditions(it, "if");
  ImGui::PopID();

  ImGui::SeparatorText("Acciones de la interaccion");
  ImGui::PushID("do");
  EditActionList(it, "do", true);
  ImGui::PopID();

  ImGui::Spacing();
  const bool remove = ImGui::Button("Quitar interaccion");
  ImGui::PopID();

  // Lo ultimo: despues de esto 'it' ya no existe.
  if (remove) {
    BeginChange(0);
    e.erase("interact");
    MarkChanged(0);
  }
}

} // namespace editor