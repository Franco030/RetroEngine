// Panel Validacion: busca en la escena lo que el cargador NO rechaza pero fallara al jugar:
// escenas, sonidos y dialogos que no existen, entidades nombradas que no existen y banderas
// sospechosas (contando tambien las que leen y cambian los dialogos).
#include "EditorApp.hpp"
#include "EditorTheme.hpp"

#include "world/Dialogue.hpp"

#include <algorithm>
#include <exception>
#include <filesystem>
#include <fstream>
#include <initializer_list>
#include <map>
#include <set>
#include <string>
#include <system_error>
#include <vector>

namespace editor {

namespace {

namespace fs = std::filesystem;

// Donde esta algo en el documento: que seleccionar y como nombrarlo en los mensajes.
struct Place {
  SelectionKind kind = SelectionKind::None;
  int index = -1;
  std::string label; // "Trigger 'cartel'", "Entidad 'llave' (interaccion)"
};

// Para cada bandera, el primer sitio donde aparece. std::map los mantiene ordenados por nombre,
// asi la lista del panel sale siempre en el mismo orden.
struct FlagUse {
  std::map<std::string, Place> read;
  std::map<std::string, Place> written;
};

// Banderas que consulta un arbol de condiciones (un objeto, un arreglo o cualquier otra cosa).
void CollectConditionFlags(const Json& c, std::set<std::string>& out) {
  if (c.is_array()) {
    for (const Json& child : c)
      CollectConditionFlags(child, out);
    return;
  }
  if (!c.is_object())
    return;
  if (c.contains("flag") && c.at("flag").is_string())
    out.insert(c.at("flag").get<std::string>());
  for (const char* key : {"all", "any", "not"}) {
    if (c.contains(key))
      CollectConditionFlags(c.at(key), out);
  }
}

// Banderas que un mensaje muestra con {nombre} (como ExpandFlags en Logic.cpp).
void CollectTextFlags(const std::string& text, std::set<std::string>& out) {
  std::size_t open = text.find('{');
  while (open != std::string::npos) {
    const std::size_t close = text.find('}', open + 1);
    if (close == std::string::npos)
      break;
    out.insert(text.substr(open + 1, close - open - 1));
    open = text.find('{', close + 1);
  }
}

// Recorre toda la logica de la escena: llama a actions(lista, sitio) por cada "do" y a
// conditions(condiciones, sitio) por cada "if" o "showIf".
//
// Es una plantilla: ActionsFn y CondFn son el tipo de cada lambda que se le pase, y el
// compilador genera una version de la funcion para cada combinacion.
template <class ActionsFn, class CondFn>
void ForEachLogic(const Json& doc, ActionsFn&& actions, CondFn&& conditions) {
  if (!doc.is_object())
    return;

  auto nameOf = [](const Json& item, const char* fallback, std::size_t i) {
    const std::string name = ReadString(&item, "name");
    return name.empty() ? fallback + std::to_string(i) : name;
  };

  if (doc.contains("triggers") && doc.at("triggers").is_array()) {
    const Json& list = doc.at("triggers");
    for (std::size_t i = 0; i < list.size(); ++i) {
      const Json& t = list[i];
      const Place place{SelectionKind::Trigger, static_cast<int>(i),
                        "Trigger '" + nameOf(t, "trigger_", i) + "'"};
      if (t.is_object() && t.contains("if"))
        conditions(t.at("if"), place);
      if (t.is_object() && t.contains("do"))
        actions(t.at("do"), place);
    }
  }

  if (doc.contains("entities") && doc.at("entities").is_array()) {
    const Json& list = doc.at("entities");
    for (std::size_t i = 0; i < list.size(); ++i) {
      const Json& e = list[i];
      if (!e.is_object())
        continue;
      const std::string label = "Entidad '" + nameOf(e, "entidad ", i) + "'";
      if (e.contains("showIf"))
        conditions(e.at("showIf"), Place{SelectionKind::Entity, static_cast<int>(i), label});
      if (e.contains("interact") && e.at("interact").is_object()) {
        const Json& it = e.at("interact");
        const Place place{SelectionKind::Entity, static_cast<int>(i), label + " (interaccion)"};
        if (it.contains("if"))
          conditions(it.at("if"), place);
        if (it.contains("do"))
          actions(it.at("do"), place);
      }
      if (e.contains("seen") && e.at("seen").is_object()) {
        const Place place{SelectionKind::Entity, static_cast<int>(i), label + " (al mirarla)"};
        for (const char* key : {"do", "away"}) {
          if (e.at("seen").contains(key))
            actions(e.at("seen").at(key), place);
        }
      }
    }
  }
}

// Banderas de una lista de acciones y de un arbol de condiciones. try_emplace NO sobrescribe:
// se queda el primer sitio en que aparece cada una.
void CollectActionFlags(const Json& list, const Place& place, FlagUse& use) {
  if (!list.is_array())
    return;
  for (const Json& a : list) {
    if (!a.is_object())
      continue;
    for (const char* key : {"set_flag", "add_flag"}) {
      if (a.contains(key) && a.at(key).is_string())
        use.written.try_emplace(a.at(key).get<std::string>(), place);
    }
    if (a.contains("message") && a.at("message").is_string()) {
      std::set<std::string> shown;
      CollectTextFlags(a.at("message").get<std::string>(), shown);
      for (const std::string& flag : shown)
        use.read.try_emplace(flag, place);
    }
  }
}

void CollectReadFlags(const Json& c, const Place& place, FlagUse& use) {
  std::set<std::string> flags;
  CollectConditionFlags(c, flags);
  for (const std::string& flag : flags)
    use.read.try_emplace(flag, place);
}

// Que banderas lee y cuales cambia la escena.
void CollectFlags(const Json& doc, FlagUse& use) {
  ForEachLogic(
      doc, [&](const Json& list, const Place& place) { CollectActionFlags(list, place, use); },
      [&](const Json& c, const Place& place) { CollectReadFlags(c, place, use); });
}

// Lo mismo para un dialogo 'id': sus condiciones, sus paginas con {bandera}, sus acciones y las
// banderas de memoria que el juego le lleva (dlg:<id> y dlg:<id>.<clave>, ver Dialogue.hpp).
void CollectDialogueFlags(const std::string& id, const Json& d, FlagUse& use) {
  if (!d.is_object() || !d.contains("entries") || !d.at("entries").is_array())
    return;
  const Place place{SelectionKind::None, -1, "Dialogo '" + id + "'"};
  use.written.try_emplace(retro::DialogueCountFlag(id), place);
  const Json& entries = d.at("entries");
  for (std::size_t i = 0; i < entries.size(); ++i) {
    const Json& e = entries[i];
    if (!e.is_object())
      continue;
    const std::string key = e.contains("key") ? ReadString(&e, "key") : std::to_string(i);
    use.written.try_emplace(retro::DialogueEntryFlag(id, key), place);
    if (e.contains("if"))
      CollectReadFlags(e.at("if"), place, use);
    if (e.contains("do"))
      CollectActionFlags(e.at("do"), place, use);
    if (e.contains("lines") && e.at("lines").is_array()) {
      std::set<std::string> shown;
      for (const Json& line : e.at("lines")) {
        if (line.is_string())
          CollectTextFlags(line.get<std::string>(), shown);
      }
      for (const std::string& flag : shown)
        use.read.try_emplace(flag, place);
    }
  }
}

bool AssetExists(const fs::path& root, const std::string& relative) {
  std::error_code ec;
  return !relative.empty() && fs::is_regular_file(root / relative, ec);
}

Issue MakeIssue(Severity severity, const Place& place, const std::string& text) {
  return Issue{severity, place.label.empty() ? text : place.label + ": " + text, place.kind,
               place.index};
}

// Todos los problemas de 'doc'. 'others' son las banderas de las DEMAS escenas: una bandera que
// se consulta aqui puede fijarse en otra escena (por ejemplo, la llave de la escena anterior).
std::vector<Issue> ValidateDocument(const Json& doc, const fs::path& root, const FlagUse& others) {
  std::vector<Issue> issues;

  std::set<std::string> entityNames;
  if (doc.is_object() && doc.contains("entities") && doc.at("entities").is_array()) {
    for (const Json& e : doc.at("entities"))
      entityNames.insert(ReadString(&e, "name"));
  }

  // --- acciones con referencias a archivos y entidades ---
  ForEachLogic(
      doc,
      [&](const Json& list, const Place& place) {
        if (!list.is_array())
          return;
        if (list.empty() && place.kind == SelectionKind::Trigger)
          issues.push_back(MakeIssue(Severity::Info, place, "no tiene acciones"));

        for (const Json& a : list) {
          if (!a.is_object())
            continue;
          const std::string scene = ReadString(&a, "goto_scene");
          if (a.contains("goto_scene") && !AssetExists(root, scene)) {
            issues.push_back(MakeIssue(Severity::Error, place,
                                       "cambia a la escena '" + scene + "', que no existe"));
          }
          for (const char* key : {"play_sound", "play_music"}) {
            const std::string file = ReadString(&a, key);
            if (a.contains(key) && !AssetExists(root, file)) {
              issues.push_back(
                  MakeIssue(Severity::Warning, place, "el sonido '" + file + "' no existe"));
            }
          }
          const std::string dialogue = ReadString(&a, "dialogue");
          if (a.contains("dialogue") && !AssetExists(root, "dialogues/" + dialogue + ".json")) {
            issues.push_back(MakeIssue(Severity::Error, place,
                                       "abre el dialogo '" + dialogue + "', que no existe"));
          }
          for (const char* key : {"look_at", "scare"}) {
            if (a.contains(key) && a.at(key).is_string() &&
                entityNames.count(a.at(key).get<std::string>()) == 0) {
              issues.push_back(MakeIssue(Severity::Error, place,
                                         "mira hacia la entidad '" +
                                             a.at(key).get<std::string>() + "', que no existe"));
            }
          }
          if (a.contains("play_sound") && a.contains("at") && a.at("at").is_string()) {
            const std::string target = a.at("at").get<std::string>();
            if (entityNames.count(target) == 0) {
              issues.push_back(MakeIssue(Severity::Error, place,
                                         "suena en la entidad '" + target + "', que no existe"));
            }
          }
        }
      },
      [](const Json&, const Place&) {});

  // --- banderas ---
  FlagUse here;
  CollectFlags(doc, here);
  for (const auto& [flag, place] : here.read) {
    if (here.written.count(flag) == 0 && others.written.count(flag) == 0) {
      issues.push_back(MakeIssue(Severity::Warning, place,
                                 "consulta la bandera '" + flag +
                                     "', que ninguna escena cambia (siempre vale 0)"));
    }
  }
  for (const auto& [flag, place] : here.written) {
    if (here.read.count(flag) == 0 && others.read.count(flag) == 0) {
      issues.push_back(MakeIssue(Severity::Info, place,
                                 "cambia la bandera '" + flag + "', que nadie consulta"));
    }
  }

  // Primero los errores, luego los avisos, luego la informacion; dentro de cada grupo, el orden
  // en que se encontraron (stable_sort no lo cambia).
  std::stable_sort(issues.begin(), issues.end(), [](const Issue& a, const Issue& b) {
    return static_cast<int>(a.severity) < static_cast<int>(b.severity);
  });
  return issues;
}

} // namespace

void EditorApp::RunValidation() {
  validatedRevision_ = docRevision_;
  issues_.clear();
  if (!doc_.is_object())
    return;

  const fs::path root(engine_.Assets().Root());

  // Las banderas de las demas escenas se leen del disco (lo guardado, no lo que tengan abierto).
  FlagUse others;
  RefreshSceneList();
  for (const std::string& path : sceneList_) {
    if (path == scenePath_)
      continue;
    try {
      std::ifstream in(root / path);
      CollectFlags(Json::parse(in), others);
    } catch (const std::exception&) {
      issues_.push_back({Severity::Warning, "No se pudo leer " + path + " para buscar banderas",
                         SelectionKind::None, -1});
    }
  }

  // Y las de los dialogos: una bandera que fija un dialogo (la llave que da un NPC) cuenta.
  RefreshDialogueList();
  for (const std::string& id : dialogueList_) {
    try {
      std::ifstream in(root / ("dialogues/" + id + ".json"));
      CollectDialogueFlags(id, Json::parse(in), others);
    } catch (const std::exception&) {
      issues_.push_back({Severity::Warning, "No se pudo leer el dialogo " + id,
                         SelectionKind::None, -1});
    }
  }

  std::vector<Issue> found = ValidateDocument(doc_, root, others);
  issues_.insert(issues_.begin(), found.begin(), found.end());
}

void EditorApp::DrawValidation() {
  // Mientras se arrastra un valor o el gizmo, el documento cambia cada fotograma: se espera a
  // soltar para no repetir el analisis (y la lectura de las demas escenas) sin parar.
  const bool busy = ImGui::IsAnyItemActive() || gizmoDrag_ >= 0;
  if (validatedRevision_ != docRevision_ && !busy)
    RunValidation();

  int errors = 0;
  int warnings = 0;
  for (const Issue& issue : issues_) {
    errors += issue.severity == Severity::Error ? 1 : 0;
    warnings += issue.severity == Severity::Warning ? 1 : 0;
  }

  // "###Validacion": el titulo muestra el contador, pero el ID de la ventana no cambia.
  const std::string title =
      ICON_FA_LIST_CHECK "  Validacion (" + std::to_string(errors + warnings) + ")###Validacion";
  if (!ImGui::Begin(title.c_str())) {
    ImGui::End();
    return;
  }

  if (ImGui::SmallButton(ICON_FA_ARROWS_ROTATE "  Volver a comprobar"))
    RunValidation();
  ImGui::SameLine();
  ImGui::TextDisabled("%d errores, %d avisos", errors, warnings);
  ImGui::Separator();

  if (issues_.empty())
    ImGui::TextColored(theme::kSuccess, ICON_FA_CIRCLE_CHECK "  Sin problemas");

  ImGui::BeginChild("validacion_lista");
  for (std::size_t i = 0; i < issues_.size(); ++i) {
    const Issue& issue = issues_[i];
    ImVec4 color = theme::kTextDim;
    const char* tag = ICON_FA_CIRCLE_INFO "  ";
    if (issue.severity == Severity::Error) {
      color = theme::kError;
      tag = ICON_FA_CIRCLE_XMARK "  ";
    } else if (issue.severity == Severity::Warning) {
      color = theme::kWarning;
      tag = ICON_FA_TRIANGLE_EXCLAMATION "  ";
    }

    ImGui::PushID(static_cast<int>(i));
    ImGui::PushStyleColor(ImGuiCol_Text, color);
    const bool clicked = ImGui::Selectable((tag + issue.text).c_str());
    ImGui::PopStyleColor();
    if (issue.kind != SelectionKind::None) {
      if (ImGui::IsItemHovered())
        ImGui::SetTooltip("Clic: seleccionar y enfocar");
      if (clicked) {
        selection_ = {issue.kind, issue.index};
        ValidateSelection();
        FocusSelection();
      }
    }
    ImGui::PopID();
  }
  ImGui::EndChild();
  ImGui::End();
}

} // namespace editor
