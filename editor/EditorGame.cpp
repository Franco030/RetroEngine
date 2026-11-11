// El panel Juego: el game.json del juego abierto (lo que hace de un juego ese juego: su nombre, su
// ejecutable, por donde empieza, sus pantallas de inicio y su intro). Y "Abrir juego": el editor
// trabaja sobre un juego cada vez; abrir otro lo relanza con los assets de ese.
#include "EditorApp.hpp"
#include "EditorTheme.hpp"

#include "game/GameConfig.hpp"
#include "game/Localization.hpp"
#include "game/SaveStorage.hpp"

#include <imgui_internal.h>

#include <raymath.h>

#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <exception>
#include <filesystem>
#include <fstream>
#include <string>
#include <system_error>
#include <vector>

namespace editor {

namespace fs = std::filesystem;

namespace {

const ImVec4 kGameError(1.0f, 0.45f, 0.4f, 1.0f);

// "Hay un valle donde..." -> lo que cabe en la cabecera de una toma.
std::string Short(const std::string& text, std::size_t n) {
  std::string line = text.substr(0, text.find('\n'));
  if (line.size() > n)
    line = line.substr(0, n) + "...";
  return line;
}

} // namespace

fs::path EditorApp::GameFolder() const {
  // los assets son <juego>/assets/: el juego es la carpeta de arriba
  return fs::path(engine_.Assets().Root()).lexically_normal().parent_path().parent_path();
}

void EditorApp::LoadGameDoc() {
  gameError_.clear();
  gameChanged_ = false;
  const fs::path file = GameFolder() / "game.json";
  try {
    std::ifstream in(file);
    if (!in)
      throw std::runtime_error("no hay " + file.generic_string());
    gameDoc_ = Json::parse(in);
    gameExe_ = retro::LoadGameConfig(file).exe;
  } catch (const std::exception& e) {
    gameError_ = e.what();
    if (!gameDoc_.is_object())
      gameDoc_ = Json::object();
  }
  LoadLanguageTables();
  Log(!gameError_.empty(), gameError_.empty() ? "Juego: " + file.generic_string() : "game.json: " + gameError_);
}

void EditorApp::SaveGameIfChanged() {
  if (!gameChanged_ || ImGui::IsAnyItemActive())
    return;
  gameChanged_ = false;
  const fs::path file = GameFolder() / "game.json";
  try {
    std::string text = jsonfmt::FormatJson(gameDoc_);
    if (Json::parse(text) != gameDoc_)
      text = gameDoc_.dump(2) + "\n";
    retro::savefile::WriteAtomic(file, text);
  } catch (const std::exception& e) {
    Log(true, std::string("No se pudo guardar el game.json: ") + e.what());
    return;
  }
  // se lee como lo leera el juego, para avisar de lo que falte
  LoadLanguageTables();
  gameError_.clear();
  try {
    gameExe_ = retro::LoadGameConfig(file).exe;
  } catch (const std::exception& e) {
    gameError_ = e.what();
  }
}

void EditorApp::LoadLanguageTables() {
  langTables_.clear();
  if (!gameDoc_.contains("languages") || !gameDoc_.at("languages").is_array())
    return;
  const Json& list = gameDoc_.at("languages");
  for (std::size_t i = 1; i < list.size(); ++i) { // el primero es el de origen
    const std::string code = list[i].value("code", std::string());
    if (!code.empty())
      langTables_.emplace_back(code, retro::lang::LoadTable({engine_.Assets().EngineRoot(), engine_.Assets().Root()}, code));
  }
}

std::vector<std::pair<std::string, std::vector<std::string>>> EditorApp::Untranslated(const Json& doc) const {
  std::vector<std::string> texts;
  auto add = [&](const Json& v) {
    if (v.is_string() && !v.get<std::string>().empty())
      texts.push_back(v.get<std::string>());
  };
  auto walk = [&](auto&& self, const Json& x) -> void {
    if (x.is_object()) {
      for (const auto& [k, v] : x.items()) {
        if (k == "prompt" || k == "message" || k == "speaker")
          add(v);
        else if (k == "lines" && v.is_array())
          for (const Json& t : v)
            add(t);
        self(self, v);
      }
    } else if (x.is_array()) {
      for (const Json& v : x)
        self(self, v);
    }
  };
  walk(walk, doc);
  std::vector<std::pair<std::string, std::vector<std::string>>> out;
  for (const auto& [code, table] : langTables_) {
    std::vector<std::string> missing;
    for (const std::string& t : texts) {
      if (!table.contains(t) && std::find(missing.begin(), missing.end(), t) == missing.end())
        missing.push_back(t);
    }
    if (!missing.empty())
      out.emplace_back(code, std::move(missing));
  }
  return out;
}

std::vector<std::string> EditorApp::GameList() const {
  std::vector<std::string> ids;
  std::error_code ec;
  for (fs::directory_iterator it(gamesDir_, ec), end; !ec && it != end; it.increment(ec)) {
    if (it->is_directory(ec) && fs::is_regular_file(it->path() / "game.json", ec))
      ids.push_back(it->path().filename().string());
  }
  std::sort(ids.begin(), ids.end());
  return ids;
}

void EditorApp::RequestOpenGame(const std::string& id) {
  if (dirty_) {
    pending_ = {PendingKind::OpenGame, id};
    openUnsavedPopup_ = true;
  } else {
    OpenGame(id);
  }
}

void EditorApp::OpenGame(const std::string& id) {
  // otro proceso del editor con los assets de ese juego, y este se cierra
  const std::string exe = std::string(GetApplicationDirectory()) + "RetroEditor.exe";
  const std::string assets = (fs::path(gamesDir_) / id / "assets").generic_string();
  const std::string command = "start \"\" \"" + exe + "\" --assets \"" + assets + "\"";
  if (std::system(command.c_str()) != 0) {
    Log(true, "No se pudo abrir el editor con " + id);
    return;
  }
  quit_ = true;
}

bool EditorApp::FieldStringList(const char* label, Json& root, const char* key) {
  std::string joined;
  if (root.contains(key) && root.at(key).is_array()) {
    for (const Json& v : root.at(key)) {
      if (v.is_string())
        joined += (joined.empty() ? "" : ", ") + v.get<std::string>();
    }
  }
  Json tmp = Json::object({{"v", joined}});
  ImGui::PushID(key);
  const bool changed = FieldText(label, tmp, nullptr, "v");
  ImGui::PopID();
  if (!changed)
    return false;
  Json list = Json::array();
  const std::string text = tmp.value("v", std::string());
  for (std::size_t start = 0; start <= text.size();) {
    const std::size_t end = std::min(text.find(',', start), text.size());
    const std::string item = Trim(text.substr(start, end - start));
    if (!item.empty())
      list.push_back(item);
    start = end + 1;
  }
  if (list.empty())
    root.erase(key);
  else
    root[key] = list;
  return true;
}

void EditorApp::EditTitleScreen(Json& t) {
  FieldFile("Escena", t, nullptr, "scene", sceneList_, false, false);
  FieldFile("Musica", t, nullptr, "music", assets_.sounds, true, false);
  FieldText("En la lista de partidas", t, nullptr, "place");
  FieldStringList("Si la escena empieza por", t, "prefixes");
  FieldStringList("O si esta la bandera", t, "flags");
  FieldBool("Titulo distorsionado", t, nullptr, "distorted", false);

  bool orbit = t.contains("orbit") && t.at("orbit").is_object();
  if (ImGui::Checkbox("Vuelta alrededor de un punto", &orbit)) {
    BeginChange(0);
    if (orbit)
      t["orbit"] = Json::object({{"center", Vec3Json({0.0f, 1.5f, 0.0f})}, {"radius", 12}, {"height", 4}});
    else
      t.erase("orbit");
    MarkChanged(0);
  }
  ImGui::SetItemTooltip("Sin esto, la camara da vueltas al 'target' de la camara de la escena.");
  if (orbit) {
    ImGui::Indent();
    FieldVec3("Centro", t, "orbit", "center", {0.0f, 0.0f, 0.0f}, 0.1f, false);
    FieldFloat("Radio", t, "orbit", "radius", 12.0f, 0.1f, 1.0f, 200.0f, false);
    FieldFloat("Altura", t, "orbit", "height", 3.0f, 0.1f, -50.0f, 100.0f, false);
    if (t.value("scene", std::string()) == scenePath_) {
      if (ImGui::Button(ICON_FA_CROSSHAIRS "  Centro = a donde mira la camara")) {
        BeginChange(0);
        const Vector3 c = Vector3Add(camera_.Position(), Vector3Scale(camera_.Forward(), 10.0f));
        t["orbit"]["center"] = Vec3Json(c);
        MarkChanged(0);
      }
      ImGui::SetItemTooltip("El punto a 10 m delante de la camara del editor.");
    }
    ImGui::Unindent();
  }
}

void EditorApp::EditIntroShot(Json& shot) {
  FieldFile("Escena", shot, nullptr, "scene", sceneList_, false, false);
  FieldFloat("Segundos", shot, nullptr, "seconds", 8.0f, 0.1f, 0.5f, 60.0f, false);
  FieldText("Texto", shot, nullptr, "text", true);
  FieldVec3("Camara desde", shot, nullptr, "eyeFrom", {0.0f, 2.0f, 5.0f}, 0.1f, false);
  FieldVec3("Mira desde", shot, nullptr, "lookFrom", {0.0f, 1.0f, 0.0f}, 0.1f, false);
  FieldVec3("Camara hasta", shot, nullptr, "eyeTo", {0.0f, 2.0f, 5.0f}, 0.1f, false);
  FieldVec3("Mira hasta", shot, nullptr, "lookTo", {0.0f, 1.0f, 0.0f}, 0.1f, false);
  // con la escena de la toma abierta: tomar la vista del editor o ir a la de la toma
  if (shot.value("scene", std::string()) != scenePath_) {
    ImGui::TextDisabled("Abre su escena para tomar la vista del editor.");
    return;
  }
  const Vector3 eye = camera_.Position();
  const Vector3 look = Vector3Add(eye, Vector3Scale(camera_.Forward(), 10.0f));
  auto take = [&](const char* eyeKey, const char* lookKey) {
    BeginChange(0);
    shot[eyeKey] = Vec3Json(eye);
    shot[lookKey] = Vec3Json(look);
    MarkChanged(0);
  };
  if (ImGui::Button(ICON_FA_VIDEO "  Inicio = esta vista"))
    take("eyeFrom", "lookFrom");
  ImGui::SameLine();
  if (ImGui::Button(ICON_FA_VIDEO "  Final = esta vista"))
    take("eyeTo", "lookTo");
  if (ImGui::Button(ICON_FA_EYE "  Ver el inicio"))
    camera_.LookFrom(ReadVec3(&shot, "eyeFrom", eye), ReadVec3(&shot, "lookFrom", look));
  ImGui::SameLine();
  if (ImGui::Button(ICON_FA_EYE "  Ver el final"))
    camera_.LookFrom(ReadVec3(&shot, "eyeTo", eye), ReadVec3(&shot, "lookTo", look));
}

void EditorApp::DrawGame() {
  if (const ImGuiWindow* inspector = ImGui::FindWindowByName("###Inspector");
      inspector && inspector->DockId != 0)
    ImGui::SetNextWindowDockID(inspector->DockId, ImGuiCond_FirstUseEver);
  if (!ImGui::Begin(ICON_FA_GAMEPAD "  Juego###Juego")) {
    ImGui::End();
    return;
  }
  ImGui::TextDisabled("%s  (se guarda solo)", (GameFolder() / "game.json").generic_string().c_str());
  if (!gameError_.empty()) {
    ImGui::PushStyleColor(ImGuiCol_Text, kGameError);
    ImGui::TextWrapped(ICON_FA_TRIANGLE_EXCLAMATION "  %s", gameError_.c_str());
    ImGui::PopStyleColor();
  }

  gameMode_ = true; // desde aqui los Field* y las listas cambian el game.json
  ImGui::PushID("juego");

  ImGui::SeparatorText("El juego");
  FieldText("Nombre", gameDoc_, nullptr, "name");
  ImGui::SetItemTooltip("La ventana y la carpeta de las fotos.");
  FieldText("Ejecutable", gameDoc_, nullptr, "exe");
  ImGui::SetItemTooltip("<exe>.exe y <exe>Debug.exe. Al cambiarlo hay que volver a configurar CMake.");
  FieldText("Carpeta de partidas", gameDoc_, nullptr, "saves");
  ImGui::SetItemTooltip("%%APPDATA%%/<carpeta>/saves. Cambiarla deja atras las partidas de antes.");
  FieldText("Icono", gameDoc_, nullptr, "icon");
  ImGui::SetItemTooltip("Una imagen de assets/ (PNG cuadrado, 256 x 256) para la ventana. El del .exe es\n"
                        "games/<id>/icon.ico: lo mete CMake al compilar.");
  FieldText("Titulo", gameDoc_, nullptr, "title");
  FieldText("Subtitulo", gameDoc_, nullptr, "subtitle");
  FieldFile("Primera escena", gameDoc_, nullptr, "firstScene", sceneList_, false, false);
  ImGui::SetItemTooltip("Donde empieza una partida nueva (despues de la intro).");
  {
    std::vector<std::string> jsons;
    std::error_code ec;
    for (fs::directory_iterator it(engine_.Assets().Root(), ec), end; !ec && it != end; it.increment(ec)) {
      if (it->is_regular_file(ec) && it->path().extension() == ".json")
        jsons.push_back(it->path().filename().string());
    }
    FieldFile("Objetos (lleva)", gameDoc_, nullptr, "items", jsons, true, false);
    ImGui::SetItemTooltip("{ \"1\": \"Una carta\", ... }: el nombre de lo que se lleva en la mano.");
  }

  // --- pantallas de inicio ---
  ImGui::SeparatorText("Pantallas de inicio (gana la primera que toque)");
  ImGui::TextDisabled("La ultima es la de siempre.");
  if (!gameDoc_.contains("titleScreens") || !gameDoc_.at("titleScreens").is_array())
    gameDoc_["titleScreens"] = Json::array();
  {
    Json& list = gameDoc_["titleScreens"];
    ListOp op = ListOp::None;
    std::size_t target = 0;
    for (std::size_t i = 0; i < list.size(); ++i) {
      ImGui::PushID(static_cast<int>(i));
      const std::string place = list[i].value("place", std::string());
      const std::string header = std::to_string(i + 1) + ". " + list[i].value("scene", std::string("(sin escena)")) +
                                 (place.empty() ? "" : "  -  " + place) + "###pantalla";
      const bool open = ImGui::TreeNodeEx(header.c_str(), ImGuiTreeNodeFlags_AllowOverlap);
      const ListOp asked = ListButtons({i, list.size(), list.size() > 1});
      if (asked != ListOp::None) {
        op = asked;
        target = i;
      }
      if (open) {
        if (list[i].is_object())
          EditTitleScreen(list[i]);
        ImGui::TreePop();
      }
      ImGui::PopID();
    }
    ApplyListOp(list, op, target);
    if (ImGui::Button(ICON_FA_PLUS "  Anadir pantalla de inicio")) {
      BeginChange(0);
      list.insert(list.begin(), Json::object({{"scene", scenePath_}, {"place", ""}}));
      MarkChanged(0);
    }
    ImGui::SetItemTooltip("Se anade al principio (con la escena abierta).");
  }

  // --- sitios con nombre ---
  ImGui::SeparatorText("Sitios en la lista de partidas");
  if (!gameDoc_.contains("places") || !gameDoc_.at("places").is_array())
    gameDoc_["places"] = Json::array();
  {
    Json& list = gameDoc_["places"];
    ListOp op = ListOp::None;
    std::size_t target = 0;
    for (std::size_t i = 0; i < list.size(); ++i) {
      ImGui::PushID(static_cast<int>(i) + 1000);
      ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x * 0.4f);
      FieldText("##prefijo", list[i], nullptr, "prefix");
      ImGui::SameLine();
      ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x * 0.5f);
      FieldText("##nombre", list[i], nullptr, "name");
      const ListOp asked = ListButtons({i, list.size(), true});
      if (asked != ListOp::None) {
        op = asked;
        target = i;
      }
      ImGui::PopID();
    }
    ApplyListOp(list, op, target);
    if (ImGui::Button(ICON_FA_PLUS "  Anadir sitio")) {
      BeginChange(0);
      list.push_back(Json::object({{"prefix", "scenes/"}, {"name", "Un sitio"}}));
      MarkChanged(0);
    }
    ImGui::SetItemTooltip("Las escenas que empiezan por el prefijo se llaman asi en la lista.");
  }

  // --- distorsion ---
  ImGui::SeparatorText("Titulo distorsionado");
  FieldText("Bandera", gameDoc_, "distortion", "flag");
  ImGui::SetItemTooltip("Cuanto mas vale, menos roto se ve: base x factor^valor.");
  FieldFloat("Base", gameDoc_, "distortion", "base", 0.85f, 0.01f, 0.0f, 1.0f);
  FieldFloat("Factor", gameDoc_, "distortion", "factor", 0.6f, 0.01f, 0.0f, 1.0f);

  // --- intro ---
  ImGui::SeparatorText("Intro de la partida nueva");
  ImGui::TextDisabled("Sin tomas, la partida nueva empieza directo en la primera escena.");
  if (!gameDoc_.contains("intro") || !gameDoc_.at("intro").is_object())
    gameDoc_["intro"] = Json::object({{"shots", Json::array()}});
  {
    Json& intro = gameDoc_["intro"];
    FieldFile("Musica##intro", intro, nullptr, "music", assets_.sounds, true, false);
    FieldFloat("Volumen##intro", intro, nullptr, "volume", 0.4f, 0.01f, 0.0f, 1.0f);
    if (!intro.contains("shots") || !intro.at("shots").is_array())
      intro["shots"] = Json::array();
    Json& shots = intro["shots"];
    ListOp op = ListOp::None;
    std::size_t target = 0;
    for (std::size_t i = 0; i < shots.size(); ++i) {
      ImGui::PushID(static_cast<int>(i) + 2000);
      const std::string header = "Toma " + std::to_string(i + 1) + ": " +
                                 Short(shots[i].value("text", std::string()), 34) + "###toma";
      const bool open = ImGui::TreeNodeEx(header.c_str(), ImGuiTreeNodeFlags_AllowOverlap);
      const ListOp asked = ListButtons({i, shots.size(), true});
      if (asked != ListOp::None) {
        op = asked;
        target = i;
      }
      if (open) {
        if (shots[i].is_object())
          EditIntroShot(shots[i]);
        ImGui::TreePop();
      }
      ImGui::PopID();
    }
    ApplyListOp(shots, op, target);
    if (ImGui::Button(ICON_FA_PLUS "  Anadir toma (desde esta vista)")) {
      BeginChange(0);
      const Vector3 eye = camera_.Position();
      const Vector3 look = Vector3Add(eye, Vector3Scale(camera_.Forward(), 10.0f));
      shots.push_back(Json::object({{"scene", scenePath_}, {"eyeFrom", Vec3Json(eye)}, {"eyeTo", Vec3Json(eye)},
                                    {"lookFrom", Vec3Json(look)}, {"lookTo", Vec3Json(look)}, {"seconds", 8},
                                    {"text", "..."}}));
      MarkChanged(0);
    }
    ImGui::SetItemTooltip("Si la toma siguiente es de la misma escena, la camara sigue sin cortar.");
  }

  EditLanguages();

  ImGui::PopID();
  gameMode_ = false;
  ImGui::End();
}

// Los idiomas del juego: el primero es el de origen (en el que se escribe todo); los demas, los que
// se pueden elegir en Configuracion, con su lang/<code>.json.
void EditorApp::EditLanguages() {
  ImGui::SeparatorText("Idiomas (el primero es el de origen)");
  if (!gameDoc_.contains("languages") || !gameDoc_.at("languages").is_array())
    gameDoc_["languages"] = Json::array({Json::object({{"code", "es"}, {"name", "Espanol"}})});
  Json& list = gameDoc_["languages"];
  ListOp op = ListOp::None;
  std::size_t target = 0;
  for (std::size_t i = 0; i < list.size(); ++i) {
    ImGui::PushID(static_cast<int>(i) + 3000);
    ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x * 0.25f);
    FieldText("##codigo", list[i], nullptr, "code");
    ImGui::SetItemTooltip("Codigo corto: es, en, fr... Su traduccion va en lang/<codigo>.json.");
    ImGui::SameLine();
    ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x * 0.55f);
    FieldText("##nombre", list[i], nullptr, "name");
    ImGui::SetItemTooltip("Como sale en Configuracion (en su idioma: English, Francais...).");
    const ListOp asked = ListButtons({i, list.size(), list.size() > 1});
    if (asked != ListOp::None) {
      op = asked;
      target = i;
    }
    ImGui::PopID();
  }
  ApplyListOp(list, op, target);
  if (ImGui::Button(ICON_FA_PLUS "  Anadir idioma")) {
    BeginChange(0);
    list.push_back(Json::object({{"code", "en"}, {"name", "English"}}));
    MarkChanged(0);
  }
}

// ---------------------------------------------------------------------------
// Nuevo juego
// ---------------------------------------------------------------------------
bool EditorApp::CreateGame(const std::string& rawName) {
  const std::string name = Trim(rawName);
  // la carpeta: minusculas y '_'; el ejecutable: las palabras juntas, con mayuscula
  std::string id, exe;
  bool upper = true;
  for (const char c : name) {
    const unsigned char u = static_cast<unsigned char>(c);
    if (u < 128 && std::isalnum(u)) {
      id += static_cast<char>(std::tolower(u));
      exe += upper ? static_cast<char>(std::toupper(u)) : c;
      upper = false;
    } else {
      if (!id.empty() && id.back() != '_')
        id += '_';
      upper = true;
    }
  }
  while (!id.empty() && id.back() == '_')
    id.pop_back();
  if (id.empty()) {
    Log(true, "Nombre de juego no valido: usa letras o numeros");
    return false;
  }
  if (std::isdigit(static_cast<unsigned char>(exe.front())))
    exe = "Juego" + exe;
  const fs::path dir = fs::path(gamesDir_) / id;
  std::error_code ec;
  if (fs::exists(dir, ec)) {
    Log(true, "Ya existe games/" + id);
    return false;
  }
  for (const std::string& other : GameList()) {
    try {
      if (retro::LoadGameConfig(fs::path(gamesDir_) / other / "game.json").exe == exe) {
        Log(true, "El ejecutable " + exe + " ya es de " + other + ": usa otro nombre");
        return false;
      }
    } catch (const std::exception&) {
      // un game.json roto no impide crear otro
    }
  }

  std::string upperName;
  for (const char c : name)
    upperName += static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
  Json game = Json::object();
  game["name"] = name;
  game["exe"] = exe;
  game["title"] = upperName;
  game["subtitle"] = "";
  game["firstScene"] = "scenes/inicio.json";
  game["languages"] = Json::array({Json::object({{"code", "es"}, {"name", "Espanol"}})});
  game["titleScreens"] = Json::array({Json::object(
      {{"scene", "scenes/inicio.json"},
       {"orbit", Json::object({{"center", Vec3Json({0.0f, 1.0f, 0.0f})}, {"radius", 12}, {"height", 4}})},
       {"place", "El inicio"}})});

  // una escena para empezar: un suelo, el jugador y la camara
  Json ground = Json::object({{"name", "suelo"},
                              {"model", Json::object({{"type", "plane"}, {"size", 40}, {"subdivisions", 40}})},
                              {"tint", Json::array({110, 128, 92, 255})}});
  Json scene = Json::object();
  scene["camera"] = Json::object(
      {{"position", Vec3Json({7.0f, 5.0f, 9.0f})}, {"target", Vec3Json({0.0f, 1.0f, 0.0f})}, {"fov", 60}});
  scene["player"] = Json::object({{"position", Vec3Json({0.0f, 0.0f, 9.0f})}, {"yaw", 0}});
  scene["autoCollide"] = true;
  scene["bounds"] = Json::object({{"min", Vec2Json({-19.0f, -19.0f})}, {"max", Vec2Json({19.0f, 19.0f})}});
  scene["entities"] = Json::array({ground});

  try {
    for (const char* sub : {"scenes", "dialogues", "models", "textures", "audio", "lang"})
      fs::create_directories(dir / "assets" / sub);
    retro::savefile::WriteAtomic(dir / "game.json", jsonfmt::FormatJson(game));
    retro::savefile::WriteAtomic(dir / "assets" / "scenes" / "inicio.json", jsonfmt::FormatJson(scene));
  } catch (const std::exception& e) {
    Log(true, std::string("No se pudo crear el juego: ") + e.what());
    return false;
  }
  Log(false, "Juego creado: games/" + id + ". Al compilar salen " + exe + ".exe y " + exe + "Debug.exe.");
  RequestOpenGame(id);
  return true;
}

void EditorApp::DrawNewGamePopup() {
  if (openNewGamePopup_) {
    newGameName_[0] = '\0';
    ImGui::OpenPopup("Nuevo juego");
    openNewGamePopup_ = false;
  }
  if (!ImGui::BeginPopupModal("Nuevo juego", nullptr, ImGuiWindowFlags_AlwaysAutoResize))
    return;
  ImGui::TextUnformatted("Nombre del juego (p. ej. \"La casa del lago\"):");
  if (ImGui::IsWindowAppearing())
    ImGui::SetKeyboardFocusHere();
  const bool enter =
      ImGui::InputText("##juego", newGameName_, sizeof newGameName_, ImGuiInputTextFlags_EnterReturnsTrue);
  ImGui::TextDisabled("Se crea games/<nombre>/ con su game.json y una escena para empezar,\n"
                      "y el editor se abre con el. Compila para tener su ejecutable.");
  ImGui::Spacing();
  if (ImGui::Button("Crear", ImVec2(110.0f, 0.0f)) || enter) {
    if (CreateGame(newGameName_))
      ImGui::CloseCurrentPopup();
  }
  ImGui::SameLine();
  if (ImGui::Button("Cancelar", ImVec2(110.0f, 0.0f)))
    ImGui::CloseCurrentPopup();
  ImGui::EndPopup();
}

// ---------------------------------------------------------------------------
// Escribir en otro idioma
// ---------------------------------------------------------------------------
void EditorApp::SelectWritingLanguage(std::size_t index) {
  SaveLangIfChanged();
  writingLang_ = index;
  langCode_.clear();
  langDoc_ = Json::object();
  if (index == 0 || !gameDoc_.contains("languages") || index >= gameDoc_.at("languages").size())
    return;
  langCode_ = gameDoc_.at("languages")[index].value("code", std::string());
  const fs::path file = fs::path(engine_.Assets().Root()) / "lang" / (langCode_ + ".json");
  std::ifstream in(file);
  if (!in)
    return; // todavia no hay traducciones: se crea al escribir la primera
  try {
    langDoc_ = Json::parse(in);
  } catch (const std::exception& e) {
    Log(true, "lang/" + langCode_ + ".json: " + e.what());
    writingLang_ = 0;
    langCode_.clear();
    langDoc_ = Json::object();
  }
}

void EditorApp::SaveLangIfChanged() {
  if (!langChanged_ || langCode_.empty() || ImGui::IsAnyItemActive())
    return;
  langChanged_ = false;
  const fs::path dir = fs::path(engine_.Assets().Root()) / "lang";
  try {
    fs::create_directories(dir);
    retro::savefile::WriteAtomic(dir / (langCode_ + ".json"), langDoc_.dump(1, ' ', false) + "\n");
  } catch (const std::exception& e) {
    Log(true, std::string("No se pudo guardar la traduccion: ") + e.what());
    return;
  }
  LoadLanguageTables(); // para que los avisos de "sin traduccion" se pongan al dia
}

// Cada texto del dialogo en el idioma de origen (gris) y debajo, su traduccion. Se guarda en
// lang/<code>.json con el texto de origen como clave: el dialogo no cambia.
void EditorApp::DrawDialogueTranslation() {
  const std::string& langName = gameDoc_.at("languages")[writingLang_].value("name", langCode_);
  ImGui::TextWrapped("Escribes en %s. Lo que escribas va a lang/%s.json; el dialogo (en el idioma de "
                     "origen) no cambia. Vacio = sale en el idioma de origen.",
                     langName.c_str(), langCode_.c_str());
  langMode_ = true;
  ImGui::PushID("traduccion");
  auto field = [&](const std::string& source, const char* label, bool multiline) {
    ImGui::PushStyleColor(ImGuiCol_Text, ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled));
    ImGui::TextWrapped("%s", source.c_str());
    ImGui::PopStyleColor();
    const bool missing = !langDoc_.contains(source);
    if (missing)
      ImGui::PushStyleColor(ImGuiCol_FrameBg, ImVec4(0.42f, 0.20f, 0.16f, 0.55f));
    ImGui::SetNextItemWidth(-FLT_MIN); // todo el ancho: no hay etiqueta, el texto de origen va encima
    FieldText(label, langDoc_, nullptr, source.c_str(), multiline);
    if (missing)
      ImGui::PopStyleColor();
  };
  const std::string speaker = dialogueDoc_.value("speaker", std::string());
  if (!speaker.empty()) {
    ImGui::SeparatorText("Quien habla");
    field(speaker, "##quien", false);
  }
  if (dialogueDoc_.contains("entries") && dialogueDoc_.at("entries").is_array()) {
    const Json& entries = dialogueDoc_.at("entries");
    for (std::size_t i = 0; i < entries.size(); ++i) {
      ImGui::PushID(static_cast<int>(i));
      ImGui::SeparatorText(("Entrada " + std::to_string(i + 1)).c_str());
      if (entries[i].contains("lines") && entries[i].at("lines").is_array()) {
        const Json& lines = entries[i].at("lines");
        for (std::size_t j = 0; j < lines.size(); ++j) {
          if (!lines[j].is_string() || lines[j].get<std::string>().empty())
            continue;
          ImGui::PushID(static_cast<int>(j));
          field(lines[j].get<std::string>(), "##linea", true);
          ImGui::PopID();
        }
      }
      ImGui::PopID();
    }
  }
  ImGui::PopID();
  langMode_ = false;
}

} // namespace editor
