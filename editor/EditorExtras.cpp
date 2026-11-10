// Lo que hace del editor un sitio del que no hace falta salir: oir la musica y los sonidos (y la
// escena entera, con sus sonidos en su sitio), editar el tiempo, el viento, la camara de fotos, los
// ojos cerrados y "near", ver en el visor a donde voltea la camara y a donde llevan las acciones,
// ver la escena con unas banderas (como en el juego) y probarla en el juego desde la camara.
#include "EditorApp.hpp"
#include "EditorTheme.hpp"

#include "game/GameState.hpp"
#include "world/Logic.hpp"

#include <imgui_internal.h>
#include <rlgl.h>

#include <raymath.h>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <exception>
#include <filesystem>
#include <fstream>
#include <set>
#include <string>
#include <vector>

namespace editor {

namespace {

namespace fs = std::filesystem;

bool IsAudioFile(const std::string& f) {
  const std::string ext = fs::path(f).extension().string();
  return ext == ".wav" || ext == ".ogg" || ext == ".mp3" || ext == ".flac";
}

// El primero de 'files' cuyo nombre contiene 'hint' (o el primero de todos).
std::string FirstLike(const std::vector<std::string>& files, const char* hint) {
  for (const std::string& f : files) {
    if (f.find(hint) != std::string::npos)
      return f;
  }
  return files.empty() ? std::string() : files.front();
}

// Pista de audio de la escena: "ruta" o { "file", "volume" }.
void ReadTrack(const Json& audio, const char* key, std::string& file, float& volume) {
  file.clear();
  volume = 1.0f;
  if (!audio.contains(key))
    return;
  const Json& t = audio.at(key);
  if (t.is_string()) {
    file = t.get<std::string>();
  } else if (t.is_object()) {
    file = t.value("file", std::string());
    volume = t.value("volume", 1.0f);
  }
}

// Todas las banderas que aparecen en un JSON: en condiciones ("flag"), en acciones (set_flag,
// add_flag) y en mensajes ({bandera}).
void CollectFlags(const Json& j, std::set<std::string>& out) {
  if (j.is_object()) {
    for (const char* key : {"flag", "set_flag", "add_flag"}) {
      if (j.contains(key) && j.at(key).is_string())
        out.insert(j.at(key).get<std::string>());
    }
    for (const auto& [k, v] : j.items())
      CollectFlags(v, out);
  } else if (j.is_array()) {
    for (const Json& v : j)
      CollectFlags(v, out);
  } else if (j.is_string()) {
    const std::string& s = j.get_ref<const std::string&>();
    for (std::size_t a = s.find('{'); a != std::string::npos; a = s.find('{', a + 1)) {
      const std::size_t b = s.find('}', a);
      if (b != std::string::npos && b > a + 1)
        out.insert(s.substr(a + 1, b - a - 1));
    }
  }
}

void DrawMarker(Vector3 p, Color c) {
  DrawSphere(p, 0.07f, c);
  DrawLine3D({p.x - 0.25f, p.y, p.z}, {p.x + 0.25f, p.y, p.z}, c);
  DrawLine3D({p.x, p.y - 0.25f, p.z}, {p.x, p.y + 0.25f, p.z}, c);
  DrawLine3D({p.x, p.y, p.z - 0.25f}, {p.x, p.y, p.z + 0.25f}, c);
}

// Una linea de trazos (lo que no es un objeto: una relacion).
void DrawDashed(Vector3 a, Vector3 b, Color c) {
  const float len = Vector3Distance(a, b);
  const int n = std::max(1, static_cast<int>(len / 0.3f));
  for (int i = 0; i < n; i += 2) {
    const Vector3 p0 = Vector3Lerp(a, b, static_cast<float>(i) / static_cast<float>(n));
    const Vector3 p1 = Vector3Lerp(a, b, std::min(1.0f, static_cast<float>(i + 1) / static_cast<float>(n)));
    DrawLine3D(p0, p1, c);
  }
}

// El jugador de pie en 'feet' mirando a 'yaw' (como el punto de inicio).
void DrawStanding(Vector3 feet, float yawDeg, Color c) {
  DrawCylinderWires(feet, 0.3f, 0.3f, 1.7f, 8, c);
  const float yaw = yawDeg * DEG2RAD;
  DrawLine3D({feet.x, feet.y + 1.5f, feet.z},
             {feet.x - std::sin(yaw) * 1.2f, feet.y + 1.5f, feet.z - std::cos(yaw) * 1.2f}, c);
}

} // namespace

// ---------------------------------------------------------------------------
// Audio
// ---------------------------------------------------------------------------
void EditorApp::UpdateAudio() {
  retro::AudioSystem& audio = engine_.Audio();
  audio.Update(GetFrameTime());
  if (!previewFile_.empty() && previewEnds_ > 0.0 && GetTime() > previewEnds_)
    previewFile_.clear();
  if (listenScene_ && scene_) {
    const Vector3 p = camera_.Position();
    audio.SetListener(p, camera_.Forward());
    emitters_.clear();
    scene_->CollectEmitters(emitters_);
    audio.UpdateEmitters(emitters_, GetFrameTime());
  }
}

void EditorApp::StopPreview() {
  if (previewFile_.empty())
    return;
  if (previewEnds_ <= 0.0) // lo largo va por el canal de la musica; lo corto se acaba solo
    engine_.Audio().StopMusic(0.15f);
  previewFile_.clear();
}

void EditorApp::AudioButton(const std::string& file, bool sameLine) {
  if (file.empty() || !IsAudioFile(file))
    return;
  if (sameLine)
    ImGui::SameLine();
  ImGui::PushID(file.c_str());
  const bool playing = previewFile_ == file;
  if (ImGui::SmallButton(playing ? ICON_FA_STOP "##oir" : ICON_FA_PLAY "##oir")) {
    if (playing) {
      StopPreview();
    } else {
      if (listenScene_)
        SetListenScene(false);
      StopPreview();
      float seconds = 0.0f;
      try {
        const Sound& s = engine_.Assets().GetSound(file)->Get();
        seconds = s.stream.sampleRate ? static_cast<float>(s.frameCount) / static_cast<float>(s.stream.sampleRate)
                                      : 0.0f;
      } catch (const std::exception& e) {
        Log(true, "No se puede oir '" + file + "': " + e.what());
      }
      retro::AudioSystem& audio = engine_.Audio();
      if (seconds > 12.0f) { // musica o ambiente: en bucle hasta pararlo
        audio.PlayMusic(file, 1.0f, 0.1f);
        previewEnds_ = 0.0;
      } else {               // un sonido: una vez
        audio.PlaySfx(file, 1.0f);
        previewEnds_ = GetTime() + std::max(0.3f, seconds);
      }
      previewFile_ = file;
    }
  }
  ImGui::SetItemTooltip(playing ? "Parar" : "Oir");
  ImGui::PopID();
}

void EditorApp::SetListenScene(bool on) {
  retro::AudioSystem& audio = engine_.Audio();
  StopPreview();
  listenScene_ = on;
  if (!on) {
    audio.StopMusic(0.5f);
    audio.PlayAmbient("", 0.0f, 0.5f);
    audio.PlayWeather("", 0.0f, 0.5f);
    audio.ClearEmitters();
    return;
  }
  std::string file;
  float volume = 1.0f;
  if (doc_.contains("audio") && doc_.at("audio").is_object()) {
    const Json& a = doc_.at("audio");
    ReadTrack(a, "music", file, volume);
    audio.PlayMusic(file, volume, 0.8f);
    ReadTrack(a, "ambient", file, volume);
    audio.PlayAmbient(file, volume, 0.8f);
  } else {
    audio.StopMusic(0.5f);
    audio.PlayAmbient("", 0.0f, 0.5f);
  }
  if (doc_.contains("weather") && doc_.at("weather").is_object()) {
    const Json& w = doc_.at("weather");
    audio.PlayWeather(w.value("sound", std::string()), w.value("volume", 0.6f) * w.value("rain", 0.0f), 0.8f);
  } else {
    audio.PlayWeather("", 0.0f, 0.5f);
  }
}

// ---------------------------------------------------------------------------
// Listas de archivos (truenos, pasos)
// ---------------------------------------------------------------------------
void EditorApp::EditFileList(Json& list, const std::vector<std::string>& files) {
  int remove = -1;
  for (std::size_t i = 0; i < list.size(); ++i) {
    ImGui::PushID(static_cast<int>(i));
    const std::string current = list.at(i).is_string() ? list.at(i).get<std::string>() : std::string();
    ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x - 64.0f);
    if (ImGui::BeginCombo("##archivo", current.c_str())) {
      for (const std::string& f : files) {
        if (ImGui::Selectable(f.c_str(), f == current) && f != current) {
          BeginChange(0);
          list.at(i) = f;
          MarkChanged(0);
        }
      }
      ImGui::EndCombo();
    }
    AudioButton(current);
    ImGui::SameLine();
    if (ImGui::SmallButton(ICON_FA_XMARK))
      remove = static_cast<int>(i);
    ImGui::PopID();
  }
  if (remove >= 0 && list.size() > 1) {
    BeginChange(0);
    list.erase(list.begin() + remove);
    MarkChanged(0);
  }
  if (!files.empty() && ImGui::SmallButton(ICON_FA_PLUS "  Anadir")) {
    BeginChange(0);
    list.push_back(list.empty() ? Json(files.front()) : Json(list.back()));
    MarkChanged(0);
  }
}

// ---------------------------------------------------------------------------
// Escena: tiempo, viento, camara de fotos, ojos
// ---------------------------------------------------------------------------
void EditorApp::InspectWeather(Json& d) {
  if (!theme::Section(ICON_FA_CLOUD_RAIN "  Tiempo (lluvia, truenos, niebla)", d.contains("weather")))
    return;
  ImGui::PushID("weather");
  if (!d.contains("weather") || !d.at("weather").is_object()) {
    ImGui::TextDisabled("Lluvia que cae alrededor de la camara, su sonido, relampagos con\n"
                        "trueno y bancos de niebla que entran y se van.");
    if (ImGui::Button(ICON_FA_PLUS "  Anadir lluvia")) {
      BeginChange(0);
      d["weather"] = Json::object({{"rain", 1}, {"sound", FirstLike(assets_.sounds, "lluvia")}, {"volume", 0.4}});
      MarkChanged(0);
    }
    ImGui::PopID();
    return;
  }
  Json& w = d["weather"];
  FieldFloat("Lluvia (0-1)", w, nullptr, "rain", 0.0f, 0.01f, 0.0f, 1.0f, false);
  FieldBool("Bajo techo (sin gotas)", w, nullptr, "indoor", false);
  FieldFile("Sonido", w, nullptr, "sound", assets_.sounds, true, false);
  FieldFloat("Volumen##lluvia", w, nullptr, "volume", 0.6f, 0.01f, 0.0f, 1.0f);

  ImGui::SeparatorText("Relampagos");
  bool truenos = w.contains("thunder");
  if (ImGui::Checkbox("Relampagos con trueno", &truenos)) {
    BeginChange(0);
    if (truenos)
      w["thunder"] = Json::object({{"every", Json::array({10, 24})}, {"sounds", Json::array({FirstLike(assets_.sounds, "trueno")})}});
    else
      w.erase("thunder");
    MarkChanged(0);
  }
  if (truenos && w.contains("thunder")) {
    FieldVec2("Cada (min, max s)", w, "thunder", "every", {10.0f, 24.0f}, 0.1f, false);
    ImGui::TextDisabled("Truenos (uno al azar en cada relampago)");
    Json& t = w["thunder"];
    if (!t.contains("sounds") || !t.at("sounds").is_array())
      t["sounds"] = Json::array({FirstLike(assets_.sounds, "trueno")});
    EditFileList(t["sounds"], assets_.sounds);
  }

  ImGui::SeparatorText("Bancos de niebla");
  bool banco = w.contains("fogBank");
  if (ImGui::Checkbox("La niebla entra a ratos", &banco)) {
    BeginChange(0);
    if (banco)
      w["fogBank"] = Json::object({{"every", Json::array({120, 240})}, {"seconds", 15}, {"fade", 4}, {"start", 1.5},
                                   {"end", 13}, {"color", Json::array({62, 66, 74})}, {"mist", 0.55}});
    else
      w.erase("fogBank");
    MarkChanged(0);
  }
  if (banco && w.contains("fogBank")) {
    FieldVec2("Cada (min, max s)##banco", w, "fogBank", "every", {120.0f, 240.0f}, 1.0f, false);
    FieldFloat("Dura (s)", w, "fogBank", "seconds", 15.0f, 0.1f, 0.5f, 120.0f, false);
    FieldFloat("Entra y sale en (s)", w, "fogBank", "fade", 3.0f, 0.1f, 0.0f, 30.0f);
    FieldFloat("Niebla desde (m)", w, "fogBank", "start", 1.0f, 0.1f, 0.0f, 100.0f);
    FieldFloat("Niebla hasta (m)", w, "fogBank", "end", 10.0f, 0.1f, 0.5f, 300.0f);
    FieldColor("Color", w, "fogBank", "color", {60, 64, 72, 255}, false);
    FieldFloat("Neblina baja", w, "fogBank", "mist", 0.6f, 0.01f, 0.0f, 1.0f);
    FieldFile("Sonido al entrar", w, "fogBank", "sound", assets_.sounds, true, false);
    FieldFloat("Volumen##banco", w, "fogBank", "volume", 0.5f, 0.01f, 0.0f, 1.0f);
  }
  if (ImGui::Button(ICON_FA_TRASH "  Quitar el tiempo")) {
    BeginChange(0);
    d.erase("weather");
    MarkChanged(0);
  }
  ImGui::PopID();
}

void EditorApp::InspectWind(Json& d) {
  if (!theme::Section(ICON_FA_WIND "  Viento que empuja", d.contains("wind")))
    return;
  ImGui::PushID("wind");
  if (!d.contains("wind") || !d.at("wind").is_object()) {
    ImGui::TextDisabled("Empuja al jugador desde un centro (o hacia el), con rachas; detras de\n"
                        "algo solido no empuja (a cubierto). Banderas: racha, a_cubierto.");
    if (ImGui::Button(ICON_FA_PLUS "  Anadir viento")) {
      const Vector3 c = PlacementPoint();
      BeginChange(0);
      d["wind"] = Json::object({{"center", Json::array({Number(c.x), Number(c.z)})}, {"outward", true}, {"strength", 1},
                                {"shelter", 4},
                                {"gust", Json::object({{"every", Json::array({5, 9})}, {"seconds", 2.8}, {"strength", 4.8},
                                                       {"sound", FirstLike(assets_.sounds, "racha")}, {"volume", 0.8}})}});
      MarkChanged(0);
    }
    ImGui::PopID();
    return;
  }
  Json& w = d["wind"];
  FieldVec2("Centro (x, z)", w, nullptr, "center", {0.0f, 0.0f}, 0.1f, false);
  if (ImGui::SmallButton(ICON_FA_CROSSHAIRS "  Centro en la vista")) {
    const Vector3 c = PlacementPoint();
    BeginChange(0);
    w["center"] = Json::array({Number(c.x), Number(c.z)});
    MarkChanged(0);
  }
  FieldBool("Hacia fuera del centro", w, nullptr, "outward", true);
  FieldFloat("Fuerza (m/s)", w, nullptr, "strength", 1.0f, 0.05f, 0.0f, 20.0f);
  FieldFloat("A cubierto a menos de (m)", w, nullptr, "shelter", 6.0f, 0.1f, 0.0f, 30.0f);
  ImGui::SeparatorText("Rachas");
  bool rachas = w.contains("gust");
  if (ImGui::Checkbox("Con rachas", &rachas)) {
    BeginChange(0);
    if (rachas)
      w["gust"] = Json::object({{"every", Json::array({5, 9})}, {"seconds", 2.8}, {"strength", 4.8}});
    else
      w.erase("gust");
    MarkChanged(0);
  }
  if (rachas && w.contains("gust")) {
    FieldVec2("Cada (min, max s)##racha", w, "gust", "every", {5.0f, 9.0f}, 0.1f, false);
    FieldFloat("Dura (s)##racha", w, "gust", "seconds", 3.0f, 0.05f, 0.1f, 30.0f);
    FieldFloat("Empuja (m/s mas)", w, "gust", "strength", 5.0f, 0.05f, 0.0f, 30.0f);
    FieldFile("Sonido##racha", w, "gust", "sound", assets_.sounds, true, false);
    FieldFloat("Volumen##racha", w, "gust", "volume", 0.8f, 0.01f, 0.0f, 1.0f);
  }
  if (ImGui::Button(ICON_FA_TRASH "  Quitar el viento")) {
    BeginChange(0);
    d.erase("wind");
    MarkChanged(0);
  }
  ImGui::PopID();
}

void EditorApp::InspectPhoto(Json& d) {
  if (!theme::Section(ICON_FA_CAMERA "  Camara de fotos (F)", d.contains("photo")))
    return;
  ImGui::PushID("photo");
  if (!d.contains("photo") || !d.at("photo").is_object()) {
    ImGui::TextDisabled("F levanta la camara, clic dispara: la bandera 'flash' vale 1 un\n"
                        "instante (lo que tenga showIf con ella sale en la foto).");
    if (ImGui::Button(ICON_FA_PLUS "  Permitir la camara")) {
      BeginChange(0);
      d["photo"] = Json::object({{"cooldown", 1.5}});
      MarkChanged(0);
    }
    ImGui::PopID();
    return;
  }
  Json& p = d["photo"];
  FieldFloat("Entre fotos (s)", p, nullptr, "cooldown", 1.5f, 0.05f, 0.2f, 30.0f, false);
  ImGui::TextDisabled("Solo se puede usar si se cumple:");
  EditConditions(p, "if");
  if (ImGui::Button(ICON_FA_TRASH "  Quitar la camara")) {
    BeginChange(0);
    d.erase("photo");
    MarkChanged(0);
  }
  ImGui::PopID();
}

void EditorApp::InspectEyes(Json& d) {
  if (!theme::Section(ICON_FA_EYE_SLASH "  Cerrar los ojos (Q)", d.contains("eyes")))
    return;
  ImGui::PushID("eyes");
  if (!d.contains("eyes") || !d.at("eyes").is_object()) {
    ImGui::TextDisabled("Con Q se cierran: la bandera 'ojos_cerrados' vale 1 (lo que tenga\n"
                        "showIf con ella aparece). Si se aguantan, gana el sueno.");
    if (ImGui::Button(ICON_FA_PLUS "  Permitir cerrar los ojos")) {
      BeginChange(0);
      d["eyes"] = Json::object({{"seconds", 9}});
      MarkChanged(0);
    }
    ImGui::PopID();
    return;
  }
  Json& e = d["eyes"];
  FieldFloat("Gana el sueno a los (s)", e, nullptr, "seconds", 9.0f, 0.1f, 0.6f, 120.0f, false);
  ImGui::SeparatorText("Cuando gana el sueno");
  if (!e.contains("sleep"))
    e["sleep"] = Json::array(); // para editarla; vacia no se guarda
  EditActionList(e, "sleep");
  if (e.contains("sleep") && e.at("sleep").is_array() && e.at("sleep").empty())
    e.erase("sleep");
  if (ImGui::Button(ICON_FA_TRASH "  Quitar")) {
    BeginChange(0);
    d.erase("eyes");
    MarkChanged(0);
  }
  ImGui::PopID();
}

void EditorApp::EditNear(Json& e) {
  if (!theme::Section(ICON_FA_EAR_LISTEN "  Al estar cerca (oirla)", e.contains("near")))
    return;
  ImGui::PushID("near");
  if (!e.contains("near") || !e.at("near").is_object()) {
    ImGui::TextDisabled("Pone una bandera si el jugador pasa un rato cerca, mire donde mire\n"
                        "(oir lo que respira), y la quita al alejarse.");
    if (ImGui::Button(ICON_FA_PLUS "  Anadir")) {
      BeginChange(0);
      e["near"] = Json::object({{"range", 5}, {"after", 2}, {"forget", 5},
                                {"flag", "cerca_" + e.value("name", std::string("entidad"))}});
      MarkChanged(0);
    }
    ImGui::PopID();
    return;
  }
  Json& n = e["near"];
  FieldText("Bandera", n, nullptr, "flag");
  FieldFloat("A menos de (m)", n, nullptr, "range", 5.0f, 0.05f, 0.1f, 100.0f, false);
  FieldFloat("Tras (s seguidos)", n, nullptr, "after", 2.0f, 0.05f, 0.0f, 60.0f, false);
  FieldFloat("Se olvida tras (s lejos)", n, nullptr, "forget", 5.0f, 0.05f, 0.0f, 120.0f, false);
  if (ImGui::Button(ICON_FA_TRASH "  Quitar")) {
    BeginChange(0);
    e.erase("near");
    MarkChanged(0);
  }
  ImGui::PopID();
}

void EditorApp::EditDrift(Json& e) {
  if (!theme::Section(ICON_FA_TRAIN "  Pasa de largo", e.contains("drift")))
    return;
  ImGui::PushID("drift");
  if (!e.contains("drift") || !e.at("drift").is_object()) {
    ImGui::TextDisabled("Avanza sin parar y da la vuelta (el paisaje por la ventana del tren),\n"
                        "o una vez y se queda (el tren que se va). Puede arrancar y frenar\n"
                        "segun unas condiciones.");
    if (ImGui::Button(ICON_FA_PLUS "  Anadir")) {
      BeginChange(0);
      e["drift"] = Json::object({{"velocity", Json::array({-12, 0, 0})}, {"length", 160}, {"ease", 6}});
      MarkChanged(0);
    }
    ImGui::PopID();
    return;
  }
  Json& d = e["drift"];
  FieldVec3("Velocidad (m/s)", d, nullptr, "velocity", {-12.0f, 0.0f, 0.0f}, 0.05f, false);
  FieldFloat("Tramo (m)", d, nullptr, "length", 100.0f, 0.5f, 1.0f, 5000.0f, false);
  FieldBool("Una vez (se queda al final)", d, nullptr, "once", false);
  if (!ReadBool(&d, "once", false))
    FieldVec3("Centro del tramo", d, nullptr, "around", {0.0f, 0.0f, 0.0f}, 0.1f);
  FieldFloat("Arranca y frena en (s)", d, nullptr, "ease", 4.0f, 0.05f, 0.0f, 60.0f);
  ImGui::TextDisabled("Anda mientras se cumpla (vacio = siempre):");
  EditConditions(d, "while");
  if (ImGui::Button(ICON_FA_TRASH "  Quitar")) {
    BeginChange(0);
    e.erase("drift");
    MarkChanged(0);
  }
  ImGui::PopID();
}

void EditorApp::InspectLantern(Json& d) {
  if (!theme::Section(ICON_FA_FIRE "  Luz en la mano", d.contains("lantern")))
    return;
  ImGui::PushID("lantern");
  if (!d.contains("lantern") || !d.at("lantern").is_object()) {
    ImGui::TextDisabled("Una luz que va con el jugador mientras se cumplan unas condiciones\n"
                        "(el farolillo encendido en la mano).");
    if (ImGui::Button(ICON_FA_PLUS "  Anadir")) {
      BeginChange(0);
      d["lantern"] = Json::object({{"if", Json::object({{"flag", "lleva"}, {"op", "=="}, {"value", 22}})},
                                   {"color", Json::array({255, 190, 120})}, {"radius", 6}, {"intensity", 1.2},
                                   {"flicker", 0.15}});
      MarkChanged(0);
    }
    ImGui::PopID();
    return;
  }
  Json& l = d["lantern"];
  FieldColor("Color", l, nullptr, "color", {255, 190, 120, 255}, false);
  FieldFloat("Alcance (m)", l, nullptr, "radius", 6.0f, 0.05f, 0.5f, 60.0f, false);
  FieldFloat("Intensidad", l, nullptr, "intensity", 1.0f, 0.01f, 0.0f, 5.0f);
  FieldFloat("Parpadeo", l, nullptr, "flicker", 0.0f, 0.01f, 0.0f, 1.0f);
  FieldVec3("Respecto a los ojos", l, nullptr, "offset", {0.25f, -0.45f, 0.35f}, 0.01f);
  ImGui::TextDisabled("Se enciende mientras se cumpla:");
  EditConditions(l, "if");
  if (ImGui::Button(ICON_FA_TRASH "  Quitar")) {
    BeginChange(0);
    d.erase("lantern");
    MarkChanged(0);
  }
  ImGui::PopID();
}

// ---------------------------------------------------------------------------
// Visor: a donde mira la camara y a donde llevan las acciones
// ---------------------------------------------------------------------------
void EditorApp::DrawActionHints(const Json& actions, Vector3 from) {
  if (!actions.is_array() || !scene_)
    return;
  for (const Json& a : actions) {
    if (!a.is_object())
      continue;
    for (const char* key : {"look_at", "scare"}) {
      if (!a.contains(key))
        continue;
      const Json& t = a.at(key);
      const Color c = std::string(key) == "scare" ? Color{255, 80, 80, 255} : Color{255, 210, 60, 255};
      if (t.is_string()) {
        if (retro::Entity* target = scene_->Find(t.get<std::string>())) {
          const Vector3 p = retro::LookPoint(*target);
          DrawMarker(p, c);
          DrawDashed(from, p, c);
        }
      } else if (IsNumberArray(t, 3)) {
        const Vector3 p = ReadVec3(&a, key, {0.0f, 0.0f, 0.0f});
        DrawMarker(p, c);
        DrawDashed(from, p, c);
      }
    }
    if (a.contains("teleport") && IsNumberArray(a.at("teleport"), 3)) {
      const Vector3 p = ReadVec3(&a, "teleport", {0.0f, 0.0f, 0.0f});
      DrawStanding(p, ReadNumber(&a, "yaw", 0.0f), {120, 255, 200, 255});
      DrawDashed(from, p, {120, 255, 200, 255});
    }
    if (a.contains("goto_scene") && a.at("goto_scene").is_string() && a.at("goto_scene").get<std::string>() == scenePath_ &&
        a.contains("position")) {
      const Vector3 p = ReadVec3(&a, "position", {0.0f, 0.0f, 0.0f});
      DrawStanding(p, ReadNumber(&a, "yaw", 0.0f), {120, 255, 200, 255});
      DrawDashed(from, p, {120, 255, 200, 255});
    }
  }
}

void EditorApp::DrawLogicHints() {
  if (!showLookHints_ || !scene_)
    return;
  // el centro del viento de la escena, si lo hay (de donde sale o hacia donde tira)
  if (doc_.contains("wind") && doc_.at("wind").is_object()) {
    const Json& w = doc_.at("wind");
    if (IsNumberArray(w.value("center", Json()), 2)) {
      const Vector3 c{w.at("center").at(0).get<float>(), 0.1f, w.at("center").at(1).get<float>()};
      const Color cyan{140, 200, 255, 255};
      DrawMarker({c.x, 1.0f, c.z}, cyan);
      DrawCircle3D(c, ReadNumber(&w, "shelter", 6.0f), {1.0f, 0.0f, 0.0f}, 90.0f, cyan);
    }
  }
  const Json* sel = SelectedJson();
  if (!sel)
    return;
  Vector3 from{};
  float radius = 0.0f;
  if (!SelectionCenter(from, radius))
    return;

  if (selection_.kind == SelectionKind::Entity && selection_.index >= 0 &&
      static_cast<std::size_t>(selection_.index) < scene_->Count()) {
    retro::Entity& e = scene_->EntityAt(static_cast<std::size_t>(selection_.index));
    // a donde voltea la camara al hablar con ella (si sus acciones dicen algo): el punto magenta
    if (e.interact) {
      const Vector3 p = retro::LookPoint(e);
      DrawMarker(p, {255, 90, 220, 255});
      DrawCircle3D(p, 0.35f, {1.0f, 0.0f, 0.0f}, 90.0f, {255, 90, 220, 255});
    }
    if (sel->contains("interact") && sel->at("interact").is_object())
      DrawActionHints(sel->at("interact").value("do", Json::array()), from);
    if (sel->contains("seen") && sel->at("seen").is_object()) {
      DrawActionHints(sel->at("seen").value("do", Json::array()), from);
      DrawActionHints(sel->at("seen").value("away", Json::array()), from);
    }
    // "near": hasta donde se oye
    if (sel->contains("near") && sel->at("near").is_object())
      DrawCircle3D({from.x, e.transform.position.y + 0.05f, from.z}, ReadNumber(&sel->at("near"), "range", 5.0f),
                   {1.0f, 0.0f, 0.0f}, 90.0f, {255, 150, 60, 255});
  } else if (selection_.kind == SelectionKind::Trigger) {
    DrawActionHints(sel->value("do", Json::array()), from);
  }
}

// ---------------------------------------------------------------------------
// Banderas: ver la escena como en el juego
// ---------------------------------------------------------------------------
std::vector<std::string> EditorApp::SceneFlags() const {
  std::set<std::string> flags;
  CollectFlags(doc_, flags);
  for (const char* builtin : {"ojos_cerrados", "flash", "relampago", "racha", "a_cubierto", "lleva"})
    flags.insert(builtin);
  for (const auto& [name, value] : previewFlags_)
    flags.insert(name);
  return {flags.begin(), flags.end()};
}

void EditorApp::ApplyFlagPreview() {
  if (!scene_)
    return;
  retro::GameState state;
  for (const auto& [name, value] : previewFlags_)
    state.Set(name, value);
  for (std::size_t i = 0; i < scene_->Count(); ++i) {
    retro::Entity& e = scene_->EntityAt(i);
    e.visible = !flagPreview_ || e.showIf.empty() || retro::AllHold(e.showIf, state);
  }
}

void EditorApp::DrawFlags() {
  // la primera vez, en una pestana junto al Inspector (como Dialogos)
  if (const ImGuiWindow* inspector = ImGui::FindWindowByName("###Inspector"); inspector && inspector->DockId != 0)
    ImGui::SetNextWindowDockID(inspector->DockId, ImGuiCond_FirstUseEver);
  if (!ImGui::Begin(ICON_FA_FLAG "  Banderas###Banderas")) {
    ImGui::End();
    return;
  }
  bool changed = false;
  if (ImGui::Checkbox("Ver la escena como en el juego", &flagPreview_))
    changed = true;
  ImGui::SetItemTooltip("Cada entidad con \"Visible solo si\" se ve o no segun estas banderas.\n"
                        "Sin marcar, el editor las ensena todas (para colocarlas).");
  ImGui::TextDisabled("Las banderas de esta escena (y las del juego). No se guardan.");
  ImGui::Separator();
  if (ImGui::BeginTable("banderas", 2, ImGuiTableFlags_RowBg | ImGuiTableFlags_SizingStretchProp)) {
    for (const std::string& f : SceneFlags()) {
      ImGui::TableNextRow();
      ImGui::TableNextColumn();
      ImGui::TextUnformatted(f.c_str());
      ImGui::TableNextColumn();
      ImGui::PushID(f.c_str());
      int v = previewFlags_.count(f) ? previewFlags_.at(f) : 0;
      ImGui::SetNextItemWidth(-1.0f);
      if (ImGui::InputInt("##v", &v)) {
        if (v == 0)
          previewFlags_.erase(f);
        else
          previewFlags_[f] = v;
        changed = true;
      }
      ImGui::PopID();
    }
    ImGui::EndTable();
  }
  ImGui::SetNextItemWidth(160.0f);
  ImGui::InputTextWithHint("##nueva", "otra bandera", newFlag_, sizeof newFlag_);
  ImGui::SameLine();
  if (ImGui::Button(ICON_FA_PLUS "  Poner a 1") && newFlag_[0]) {
    previewFlags_[Sanitize(newFlag_)] = 1;
    newFlag_[0] = '\0';
    changed = true;
  }
  if (ImGui::Button(ICON_FA_ERASER "  Todas a 0")) {
    previewFlags_.clear();
    changed = true;
  }
  if (changed)
    ApplyFlagPreview();
  ImGui::End();
}

// ---------------------------------------------------------------------------
// Probar aqui
// ---------------------------------------------------------------------------
void EditorApp::PlayTestHere() {
  // la escena tal como esta en el editor (aunque no este guardada), en un archivo aparte
  const std::string relative = "scenes/_prueba_editor.json";
  const fs::path root = engine_.Assets().Root();
  try {
    std::ofstream out(root / relative, std::ios::binary | std::ios::trunc);
    out << doc_.dump(2);
  } catch (const std::exception& e) {
    Log(true, std::string("No se pudo preparar la prueba: ") + e.what());
    return;
  }
  // de pie donde esta la camara del editor: sobre lo que tenga debajo (o el suelo)
  const Vector3 cam = camera_.Position();
  float ground = 0.0f;
  std::vector<retro::WorldBox> boxes;
  if (scene_)
    scene_->CollectColliders(boxes);
  for (const retro::WorldBox& b : boxes) {
    if (b.top <= cam.y && b.top > ground && cam.x >= b.center.x - b.halfX && cam.x <= b.center.x + b.halfX &&
        cam.z >= b.center.z - b.halfZ && cam.z <= b.center.z + b.halfZ)
      ground = b.top;
  }
  const Vector3 f = camera_.Forward();
  const float yaw = std::atan2(-f.x, -f.z) * RAD2DEG;

  std::string flags;
  if (flagPreview_) {
    for (const auto& [name, value] : previewFlags_)
      flags += (flags.empty() ? "" : ",") + name + "=" + std::to_string(value);
  }
  char pos[96];
  std::snprintf(pos, sizeof pos, "%.2f,%.2f,%.2f", cam.x, ground, cam.z);
  // la version de depuracion: la unica que acepta --scene, --pos, --flags...
  const std::string exe = std::string(GetApplicationDirectory()) + gameExe_ + "Debug.exe";
  std::string command = "start \"\" \"" + exe + "\" --prueba --windowed --assets \"" + root.string() + "\" --scene " +
                        relative + " --pos " + pos + " --yaw " + std::to_string(yaw);
  if (!flags.empty())
    command += " --flags " + flags;
  if (std::system(command.c_str()) != 0)
    Log(true, "No se pudo abrir el juego: " + exe);
  else
    Log(false, "Probando la escena en el juego (no guarda nada).");
}

} // namespace editor
