#include "EditorApp.hpp"
#include "EditorTheme.hpp"

#include "game/SaveStorage.hpp"
#include "world/Dialogue.hpp"

#include <imgui_internal.h>

#include <algorithm>
#include <array>
#include <cctype>
#include <cstddef>
#include <cstdio>
#include <exception>
#include <filesystem>
#include <fstream>
#include <optional>
#include <stdexcept>
#include <string>
#include <system_error>
#include <utility>
#include <vector>

namespace editor {

namespace {

struct ActionInfo {
  const char* key;
  const char* title;
};

constexpr std::array<ActionInfo, 11> kActions{{
    {"dialogue", "Dialogo"},
    {"look_at", "Mirar hacia"},
    {"scare", "Susto de camara"},
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
               const std::vector<std::string>& sounds,
               const std::vector<std::string>& dialogues = {}) {
  const std::string firstScene = scenes.empty() ? std::string() : scenes.front();
  const std::string firstSound = sounds.empty() ? std::string() : sounds.front();

  if (key == "dialogue")
    return Json::object({{"dialogue", dialogues.empty() ? std::string() : dialogues.front()}});
  if (key == "look_at")
    return Json::object({{"look_at", Vec3Json({0.0f, 1.6f, 0.0f})}});
  if (key == "scare")
    return Json::object({{"scare", Vec3Json({0.0f, 1.6f, 0.0f})}});
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

  // En la misma fila que la cabecera del elemento, pegados a la derecha. Los tres iconos tienen
  // el mismo ancho (GlyphMinAdvanceX en EditorTheme.cpp).
  const ImGuiStyle& style = ImGui::GetStyle();
  const float button = ImGui::CalcTextSize(ICON_FA_TRASH).x + 8.0f;
  const float width = button * 3.0f + style.ItemSpacing.x * 2.0f;
  ImGui::SameLine(ImGui::GetContentRegionMax().x - width);

  ImGui::BeginDisabled(slot.index == 0);
  if (theme::IconButton(ICON_FA_ARROW_UP, "Subir"))
    op = ListOp::Up;
  ImGui::EndDisabled();
  ImGui::SameLine();

  ImGui::BeginDisabled(slot.index + 1 >= slot.count);
  if (theme::IconButton(ICON_FA_ARROW_DOWN, "Bajar"))
    op = ListOp::Down;
  ImGui::EndDisabled();
  ImGui::SameLine();

  ImGui::BeginDisabled(!slot.canRemove);
  if (theme::IconButton(ICON_FA_TRASH, "Quitar"))
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
    // AllowOverlap: los botones de la derecha se pueden pulsar aunque esten sobre la cabecera.
    const bool open = ImGui::TreeNodeEx(
        header.c_str(), ImGuiTreeNodeFlags_DefaultOpen | ImGuiTreeNodeFlags_AllowOverlap);
    const ListOp asked = ListButtons({i, list.size(), !(keepOne && list.size() == 1)});
    if (asked != ListOp::None) {
      op = asked;
      target = i;
    }
    if (open) {
      EditAction(list[i]);
      ImGui::TreePop();
    }
    ImGui::PopID();
  }
  ApplyListOp(list, op, target);

  if (ImGui::BeginCombo("##nueva_accion", ICON_FA_PLUS "  Anadir accion")) {
    if (ImGui::IsWindowAppearing()) // al abrirse (no a cada fotograma): por si hay dialogos nuevos
      RefreshDialogueList();
    for (const ActionInfo& info : kActions) {
      if (ImGui::Selectable(info.title)) {
        BeginChange(0);
        list.push_back(NewAction(info.key, sceneList_, assets_.sounds, dialogueList_));
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

  if (key == "dialogue") {
    if (dialogueList_.empty())
      RefreshDialogueList();
    FieldFile("Dialogo", a, nullptr, "dialogue", dialogueList_, false, false);
    const std::string id = ReadString(&a, "dialogue");
    const bool exists =
        std::find(dialogueList_.begin(), dialogueList_.end(), id) != dialogueList_.end();
    if (exists) {
      if (ImGui::Button(ICON_FA_PEN "  Editar dialogo")) {
        pendingDialogue_ = id; // se abre en su panel al dibujarlo (no a mitad de esta lista)
        focusDialogues_ = true;
      }
    } else {
      ImGui::TextDisabled("Elige uno, o crealo en el panel Dialogos");
    }

  } else if (key == "look_at" || key == "scare") {
    // A donde: una entidad por su nombre ("look_at": "nombre") o un punto ([x,y,z]).
    const char* k = info->key;
    const char* modes[] = {"Una entidad", "Un punto"};
    const int mode = a.at(k).is_string() ? 0 : 1;
    int next = mode;
    if (ImGui::Combo("Hacia", &next, modes, 2) && next != mode) {
      BeginChange(0);
      if (next == 0) {
        const std::vector<std::string> names = EntityNames();
        a[k] = names.empty() ? std::string() : names.front();
      } else {
        a[k] = Vec3Json({0.0f, 1.6f, 0.0f});
      }
      MarkChanged(0);
    }
    if (a.at(k).is_string())
      FieldFile("Entidad", a, nullptr, k, EntityNames(), false, false);
    else
      FieldVec3("Punto", a, nullptr, k, {0.0f, 1.6f, 0.0f}, 0.05f, false);
    if (key == "look_at") {
      FieldFloat("Rapidez", a, nullptr, "speed", 6.0f, 0.1f, 0.5f, 60.0f);
      ImGui::TextDisabled("Gira suave, sin acercar, mientras dure el mensaje o dialogo.\n"
                          "Ponla antes del mensaje o el dialogo.");
    } else {
      FieldFloat("Rapidez", a, nullptr, "speed", 20.0f, 0.1f, 0.5f, 60.0f);
      FieldFloat("Acercar (fov)", a, nullptr, "fov", 30.0f, 0.5f, 1.0f, 120.0f);
      ImGui::TextDisabled("Giro de golpe y acercamiento mientras dure el mensaje o dialogo.\n"
                          "Rapidez 2-3: la vista se va sola; 20+: susto.");
    }

  } else if (key == "message") {
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
  if (ImGui::BeginCombo(id, ICON_FA_PLUS "  Anadir condicion")) {
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
  const bool open = ImGui::TreeNodeEx(
      header.c_str(), ImGuiTreeNodeFlags_DefaultOpen | ImGuiTreeNodeFlags_AllowOverlap);
  const ListOp op = slot ? ListButtons(*slot) : ListOp::None;
  if (!open)
    return op;

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
      const BoundingBox b = EntityBox(entityIndex, true);
      it["box"] = Json::object({{"min", Vec3Json(b.min)}, {"max", Vec3Json(b.max)}});
    } else {
      it.erase("box");
    }
    MarkChanged(0);
  }
  if (it.contains("box")) {
    FieldBox(it["box"]);
    ImGui::TextDisabled("Sin escalar ni girar. En el visor: cian");
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

  const bool hasMessages =
      it.contains("do") && it.at("do").is_array() &&
      std::any_of(it.at("do").begin(), it.at("do").end(),
                  [](const Json& a) { return a.is_object() && a.contains("message"); });
  bool toDialogue = false;
  if (hasMessages) {
    toDialogue = ImGui::Button(ICON_FA_COMMENTS "  Pasar mensajes a un dialogo");
    ImGui::SetItemTooltip("Crea dialogues/<nombre>.json con estos textos como paginas y pone la\n"
                          "accion Dialogo en su lugar. Luego se le anaden respuestas distintas.");
  }

  ImGui::Spacing();
  const bool remove = ImGui::Button("Quitar interaccion");
  ImGui::PopID();

  if (toDialogue && !remove)
    MessagesToDialogue(e);

  // Lo ultimo: despues de esto 'it' ya no existe.
  if (remove) {
    BeginChange(0);
    e.erase("interact");
    MarkChanged(0);
  }
}

// ---------------------------------------------------------------------------
// Al mirarla ("seen")
// ---------------------------------------------------------------------------
void EditorApp::EditSight(Json& e) {
  if (!e.contains("seen") || !e.at("seen").is_object()) {
    if (ImGui::Button("Anadir \"al mirarla\"")) {
      BeginChange(0);
      e["seen"] = Json::object({{"range", 12}, {"angle", 20}, {"do", Json::array()}});
      MarkChanged(0);
    }
    ImGui::TextDisabled("Acciones cuando el jugador la ve (sin pulsar nada)");
    return;
  }
  Json& s = e["seen"];
  ImGui::PushID("seen");
  FieldFloat("Distancia", s, nullptr, "range", 15.0f, 0.1f, 0.5f, 100.0f);
  FieldFloat("Angulo", s, nullptr, "angle", 20.0f, 0.2f, 1.0f, 89.0f);
  ImGui::TextDisabled("Cuenta como vista si esta a menos de ese angulo del centro de la\n"
                      "vista y no hay nada solido en medio. Una vez por visita a la escena.");
  for (const char* key : {"do", "away"}) {
    if (!s.contains(key))
      s[key] = Json::array(); // para editarla; vacia no se guarda (ver abajo)
  }
  ImGui::SeparatorText("Al verla");
  ImGui::PushID("do");
  EditActionList(s, "do");
  ImGui::PopID();
  ImGui::SeparatorText("Al dejar de mirarla");
  ImGui::PushID("away");
  EditActionList(s, "away");
  ImGui::PopID();
  for (const char* key : {"do", "away"}) {
    if (s.contains(key) && s.at(key).is_array() && s.at(key).empty())
      s.erase(key);
  }
  const bool remove = ImGui::Button("Quitar \"al mirarla\"");
  ImGui::PopID();
  if (remove) {
    BeginChange(0);
    e.erase("seen");
    MarkChanged(0);
  }
}

// ---------------------------------------------------------------------------
// Dialogos (panel aparte: cada uno es un archivo de assets/dialogues/)
// ---------------------------------------------------------------------------
namespace {

namespace fs = std::filesystem;

// "if" de una entrada en una linea: una condicion, o varias unidas con "y".
std::string ConditionsSummary(const Json& entry) {
  if (!entry.contains("if"))
    return {};
  const Json& c = entry.at("if");
  if (!c.is_array())
    return ConditionSummary(c);
  std::string out;
  for (const Json& item : c)
    out += (out.empty() ? "" : " y ") + ConditionSummary(item);
  return out;
}

std::string FirstLine(const Json& entry) {
  if (!entry.contains("lines") || !entry.at("lines").is_array() || entry.at("lines").empty() ||
      !entry.at("lines")[0].is_string())
    return "(sin texto)";
  std::string s = entry.at("lines")[0].get<std::string>();
  for (char& c : s) {
    if (c == '\n')
      c = ' ';
  }
  return "\"" + Shorten(s, 34) + "\"";
}

// Lo que se ve en la cabecera de cada entrada: cuando sale y como empieza.
std::string EntrySummary(const Json& entry, std::size_t index) {
  std::string text = std::to_string(index + 1) + ". ";
  if (ReadBool(&entry, "once", false))
    text += "[1 vez] ";
  const std::string cond = ConditionsSummary(entry);
  if (!cond.empty())
    text += "[si " + Shorten(cond, 28) + "] ";
  return text + FirstLine(entry);
}

// Primera vez algo, despues otra cosa que se repite: lo que mas se usa.
Json DialogueTemplate(const std::string& speaker) {
  return Json::object(
      {{"speaker", speaker},
       {"entries", Json::array({Json::object({{"once", true},
                                              {"lines", Json::array({"Lo que dice la primera vez."})}}),
                                Json::object({{"lines", Json::array({"Lo que repite despues."})}})})}});
}

} // namespace

std::string EditorApp::DialoguePath(const std::string& id) const {
  return "dialogues/" + id + ".json";
}

void EditorApp::RefreshDialogueList() {
  dialogueList_.clear();
  const fs::path dir = fs::path(engine_.Assets().Root()) / "dialogues";
  std::error_code ec;
  if (!fs::exists(dir, ec))
    return;
  for (fs::recursive_directory_iterator it(dir, ec), end; !ec && it != end; it.increment(ec)) {
    if (!it->is_regular_file(ec) || it->path().extension() != ".json")
      continue;
    fs::path rel = fs::relative(it->path(), dir, ec);
    rel.replace_extension();
    dialogueList_.push_back(rel.generic_string());
  }
  std::sort(dialogueList_.begin(), dialogueList_.end());
}

void EditorApp::OpenDialogue(const std::string& id) {
  SaveDialogueIfChanged();
  try {
    std::ifstream in(fs::path(engine_.Assets().Root()) / DialoguePath(id));
    if (!in)
      throw std::runtime_error("no existe");
    dialogueDoc_ = Json::parse(in);
    dialogueId_ = id;
    CheckDialogue();
  } catch (const std::exception& e) {
    Log(true, "No se pudo abrir el dialogo '" + id + "': " + e.what());
  }
}

bool EditorApp::CreateDialogue(const std::string& id, Json content) {
  if (id.empty()) {
    Log(true, "Nombre de dialogo no valido: usa letras o numeros");
    return false;
  }
  const fs::path path = fs::path(engine_.Assets().Root()) / DialoguePath(id);
  std::error_code ec;
  if (fs::exists(path, ec)) {
    Log(true, "Ya existe " + DialoguePath(id));
    return false;
  }
  try {
    fs::create_directories(path.parent_path(), ec);
    retro::savefile::WriteAtomic(path, jsonfmt::FormatJson(content));
  } catch (const std::exception& e) {
    Log(true, std::string("No se pudo crear el dialogo: ") + e.what());
    return false;
  }
  Log(false, "Dialogo creado: " + DialoguePath(id));
  RefreshDialogueList();
  OpenDialogue(id);
  return true;
}

// Se guarda solo, cuando se suelta el control que se estaba usando (no a cada letra o arrastre).
void EditorApp::SaveDialogueIfChanged() {
  if (!dialogueChanged_ || dialogueId_.empty() || ImGui::IsAnyItemActive())
    return;
  dialogueChanged_ = false;
  try {
    std::string text = jsonfmt::FormatJson(dialogueDoc_);
    if (Json::parse(text) != dialogueDoc_)
      text = dialogueDoc_.dump(2) + "\n";
    retro::savefile::WriteAtomic(fs::path(engine_.Assets().Root()) / DialoguePath(dialogueId_),
                                 text);
  } catch (const std::exception& e) {
    Log(true, std::string("No se pudo guardar el dialogo: ") + e.what());
  }
  CheckDialogue();
}

void EditorApp::CheckDialogue() {
  dialogueError_.clear();
  try {
    retro::LoadDialogue(engine_.Assets().Root(), DialoguePath(dialogueId_));
  } catch (const std::exception& e) {
    dialogueError_ = e.what();
  }
}

bool EditorApp::EditLine(const char* label, Json& value) {
  const std::string current = value.is_string() ? value.get<std::string>() : std::string();
  const ImGuiID id = ImGui::GetID(label);
  const std::string sid = std::to_string(id);
  const bool editing = textId_ == sid;

  char buffer[sizeof textBuf_];
  std::snprintf(buffer, sizeof buffer, "%s", (editing ? std::string(textBuf_) : current).c_str());
  if (ImGui::InputTextMultiline(label, buffer, sizeof buffer,
                                ImVec2(-1.0f, ImGui::GetTextLineHeight() * 3.2f))) {
    textId_ = sid;
    std::snprintf(textBuf_, sizeof textBuf_, "%s", buffer);
  }
  if (ImGui::IsItemDeactivatedAfterEdit()) {
    const std::string next = textBuf_;
    textId_.clear();
    if (next == current)
      return false;
    BeginChange(0);
    value = next;
    MarkChanged(0);
    return true;
  }
  if (editing && !ImGui::IsItemActive())
    textId_.clear();
  return false;
}

void EditorApp::EditDialogueEntry(Json& entry, std::size_t index) {
  FieldBool("Solo la primera vez", entry, nullptr, "once", false);
  FieldText("Clave", entry, nullptr, "key");
  ImGui::SetItemTooltip("Nombre de su bandera dlg:%s.<clave>. Sin clave se usa su numero (%d):\n"
                        "ponle una si vas a reordenar entradas de 'primera vez'.",
                        dialogueId_.c_str(), static_cast<int>(index));

  ImGui::SeparatorText("Cuando sale");
  ImGui::PushID("if");
  EditConditions(entry, "if");
  ImGui::PopID();

  ImGui::SeparatorText("Paginas (E pasa a la siguiente)");
  if (!entry.contains("lines") || !entry.at("lines").is_array())
    entry["lines"] = Json::array();
  Json& lines = entry["lines"];
  ListOp op = ListOp::None;
  std::size_t target = 0;
  for (std::size_t i = 0; i < lines.size(); ++i) {
    ImGui::PushID(static_cast<int>(i));
    ImGui::TextDisabled("Pagina %d", static_cast<int>(i + 1));
    const ListOp asked = ListButtons({i, lines.size(), lines.size() > 1});
    if (asked != ListOp::None) {
      op = asked;
      target = i;
    }
    EditLine("##linea", lines[i]);
    ImGui::PopID();
  }
  ApplyListOp(lines, op, target);
  if (ImGui::Button(ICON_FA_PLUS "  Anadir pagina")) {
    BeginChange(0);
    lines.push_back("...");
    MarkChanged(0);
  }
  ImGui::TextDisabled("{bandera} muestra su valor. Enter parte la linea.");

  ImGui::SeparatorText("Al terminar");
  // "do" es opcional en el archivo; para editarlo hace falta la lista (vacia no se guarda).
  const bool hadDo = entry.contains("do");
  if (!hadDo)
    entry["do"] = Json::array();
  ImGui::PushID("do");
  EditActionList(entry, "do");
  ImGui::PopID();
  if (entry.contains("do") && entry.at("do").is_array() && entry.at("do").empty())
    entry.erase("do");
}

void EditorApp::DrawDialogues() {
  if (focusDialogues_) {
    ImGui::SetNextWindowFocus();
    focusDialogues_ = false;
  }
  // Con una disposicion guardada de antes de existir este panel, la primera vez se acopla como
  // pestana del Inspector (en vez de flotar pequeno en una esquina).
  if (const ImGuiWindow* inspector = ImGui::FindWindowByName("###Inspector");
      inspector && inspector->DockId != 0)
    ImGui::SetNextWindowDockID(inspector->DockId, ImGuiCond_FirstUseEver);
  if (!ImGui::Begin(ICON_FA_COMMENTS "  Dialogos###Dialogos")) {
    ImGui::End();
    return;
  }
  if (!pendingDialogue_.empty()) {
    RefreshDialogueList();
    OpenDialogue(pendingDialogue_);
    pendingDialogue_.clear();
  }

  // --- elegir o crear ---
  if (ImGui::BeginCombo("Dialogo", dialogueId_.empty() ? "(ninguno)" : dialogueId_.c_str())) {
    if (ImGui::IsWindowAppearing())
      RefreshDialogueList();
    for (const std::string& id : dialogueList_) {
      if (ImGui::Selectable(id.c_str(), id == dialogueId_) && id != dialogueId_)
        pendingDialogue_ = id;
    }
    ImGui::EndCombo();
  }
  ImGui::InputTextWithHint("##nuevo", "nombre del dialogo nuevo", newDialogueName_,
                           sizeof newDialogueName_);
  ImGui::SameLine();
  if (ImGui::Button(ICON_FA_PLUS "  Crear")) {
    const std::string id = Sanitize(Trim(newDialogueName_));
    std::string speaker = id;
    std::replace(speaker.begin(), speaker.end(), '_', ' ');
    if (!speaker.empty())
      speaker[0] = static_cast<char>(std::toupper(static_cast<unsigned char>(speaker[0])));
    if (CreateDialogue(id, DialogueTemplate(speaker)))
      newDialogueName_[0] = '\0';
  }

  if (dialogueId_.empty()) {
    ImGui::Spacing();
    ImGui::TextWrapped("Un dialogo es una lista de entradas. Al hablar sale la PRIMERA que se "
                       "cumpla; las de 'Solo la primera vez' ya dichas se saltan. Asi un NPC "
                       "dice A, luego B y despues repite C.");
    ImGui::TextWrapped("Se usa con la accion 'Dialogo' (en una interaccion o un trigger). En la "
                       "interaccion de una entidad, 'Pasar mensajes a un dialogo' lo crea solo.");
    ImGui::End();
    return;
  }

  ImGui::TextDisabled("assets/%s  (se guarda solo)", DialoguePath(dialogueId_).c_str());
  if (!dialogueError_.empty()) {
    ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.0f, 0.45f, 0.4f, 1.0f));
    ImGui::TextWrapped(ICON_FA_TRIANGLE_EXCLAMATION "  %s", dialogueError_.c_str());
    ImGui::PopStyleColor();
  }

  dialogueMode_ = true; // desde aqui los Field* y las listas cambian el dialogo
  ImGui::PushID("dialogo");
  FieldText("Quien habla", dialogueDoc_, nullptr, "speaker");

  ImGui::SeparatorText("Entradas (gana la primera que se cumpla)");
  if (!dialogueDoc_.contains("entries") || !dialogueDoc_.at("entries").is_array())
    dialogueDoc_["entries"] = Json::array();
  Json& entries = dialogueDoc_["entries"];
  ListOp op = ListOp::None;
  std::size_t target = 0;
  for (std::size_t i = 0; i < entries.size(); ++i) {
    ImGui::PushID(static_cast<int>(i));
    const std::string header = EntrySummary(entries[i], i) + "###entrada";
    const bool open = ImGui::TreeNodeEx(
        header.c_str(), ImGuiTreeNodeFlags_DefaultOpen | ImGuiTreeNodeFlags_AllowOverlap);
    const ListOp asked = ListButtons({i, entries.size(), entries.size() > 1});
    if (asked != ListOp::None) {
      op = asked;
      target = i;
    }
    if (open) {
      if (entries[i].is_object())
        EditDialogueEntry(entries[i], i);
      ImGui::TreePop();
    }
    ImGui::PopID();
  }
  ApplyListOp(entries, op, target);
  if (ImGui::Button(ICON_FA_PLUS "  Anadir entrada")) {
    BeginChange(0);
    entries.push_back(Json::object({{"lines", Json::array({"..."})}}));
    MarkChanged(0);
  }
  ImGui::PopID();
  dialogueMode_ = false;

  ImGui::SeparatorText("Memoria (banderas, se guardan con la partida)");
  ImGui::TextDisabled("%s = veces que se ha hablado", retro::DialogueCountFlag(dialogueId_).c_str());
  ImGui::TextDisabled("%s = 1 si esa entrada ya salio",
                      retro::DialogueEntryFlag(dialogueId_, "<clave>").c_str());
  ImGui::TextDisabled("Se pueden usar en condiciones de este y de otros sitios.");
  ImGui::End();
}

void EditorApp::MessagesToDialogue(Json& entity) {
  Json& list = entity["interact"]["do"];
  Json lines = Json::array();
  for (const Json& a : list) {
    if (a.is_object() && a.contains("message") && a.at("message").is_string())
      lines.push_back(a.at("message"));
  }
  if (lines.empty())
    return;

  const std::string base = Sanitize(ReadString(&entity, "name"));
  std::string id = base.empty() ? "dialogo" : base;
  RefreshDialogueList();
  for (int n = 2; std::find(dialogueList_.begin(), dialogueList_.end(), id) != dialogueList_.end();
       ++n)
    id = (base.empty() ? "dialogo" : base) + "_" + std::to_string(n);

  std::string speaker = ReadString(&entity, "name");
  std::replace(speaker.begin(), speaker.end(), '_', ' ');
  if (!speaker.empty())
    speaker[0] = static_cast<char>(std::toupper(static_cast<unsigned char>(speaker[0])));
  Json content = Json::object(
      {{"speaker", speaker}, {"entries", Json::array({Json::object({{"lines", lines}})})}});
  if (!CreateDialogue(id, std::move(content)))
    return;

  // El dialogo ocupa el sitio del primer mensaje; las demas acciones se quedan como estaban.
  BeginChange(0);
  Json next = Json::array();
  bool placed = false;
  for (Json& a : list) {
    if (a.is_object() && a.contains("message")) {
      if (!placed)
        next.push_back(Json::object({{"dialogue", id}}));
      placed = true;
    } else {
      next.push_back(std::move(a));
    }
  }
  list = std::move(next);
  MarkChanged(0);
  focusDialogues_ = true;
}

} // namespace editor