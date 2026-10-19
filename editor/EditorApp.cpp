#include "EditorApp.hpp"

#include "game/SaveStorage.hpp"
#include "graphics/DebugDraw.hpp"
#include "graphics/RetroCamera.hpp"
#include "physics/Collision.hpp"
#include "world/SceneLoader.hpp"

#include <imgui_internal.h>
#include <raymath.h>
#include <rlImGui.h>
#include <rlgl.h>

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <initializer_list>
#include <stdexcept>

namespace editor {

namespace {

namespace fs = std::filesystem;

const ImVec4 kAccent(0.45f, 0.75f, 1.0f, 1.0f);
const ImVec4 kError(1.0f, 0.45f, 0.45f, 1.0f);

constexpr std::size_t kMaxUndo = 200;

const char* KeyOf(SelectionKind kind) {
  switch (kind) {
  case SelectionKind::Entity:
    return "entities";
  case SelectionKind::Trigger:
    return "triggers";
  case SelectionKind::Light:
    return "lights";
  case SelectionKind::Emitter:
    return "emitters";
  case SelectionKind::None:
    break;
  }
  return nullptr;
}

const char* KindTitle(SelectionKind kind) {
  switch (kind) {
  case SelectionKind::Entity:
    return "Entidad";
  case SelectionKind::Trigger:
    return "Trigger";
  case SelectionKind::Light:
    return "Luz";
  case SelectionKind::Emitter:
    return "Sonido";
  case SelectionKind::None:
    break;
  }
  return "";
}

std::string ItemLabel(SelectionKind kind, int index, const Json& item) {
  switch (kind) {
  case SelectionKind::Entity: {
    const std::string name = ReadString(&item, "name");
    const bool sprite = item.is_object() && item.contains("sprite");
    const bool hidden = ReadBool(&item, "visible", true) == false;
    return std::string(sprite ? "[sprite] " : "[modelo] ") +
           (name.empty() ? "entidad " + std::to_string(index) : name) + (hidden ? " (oculta)" : "");
  }
  case SelectionKind::Trigger: {
    const std::string name = ReadString(&item, "name");
    return name.empty() ? "trigger " + std::to_string(index) : name;
  }
  case SelectionKind::Light:
    return "luz " + std::to_string(index);
  case SelectionKind::Emitter: {
    const std::string file = ReadString(&item, "file");
    return file.empty() ? "sonido " + std::to_string(index) : fs::path(file).filename().string();
  }
  case SelectionKind::None:
    break;
  }
  return {};
}

void ShowJsonValue(const std::string& label, const Json& v) {
  if (v.is_object() || v.is_array()) {
    const bool shortNumbers =
        v.is_array() && v.size() <= 4 &&
        std::all_of(v.begin(), v.end(), [](const Json& x) { return x.is_number(); });
    if (shortNumbers) {
      ImGui::Text("%s: %s", label.c_str(), v.dump().c_str());
      return;
    }
    if (ImGui::TreeNode(label.c_str())) {
      if (v.is_object()) {
        for (auto it = v.begin(); it != v.end(); ++it)
          ShowJsonValue(it.key(), it.value());
      } else {
        for (std::size_t i = 0; i < v.size(); ++i)
          ShowJsonValue("[" + std::to_string(i) + "]", v[i]);
      }
      ImGui::TreePop();
    }
    return;
  }
  const std::string text = v.is_string() ? v.get<std::string>() : v.dump();
  ImGui::TextWrapped("%s: %s", label.c_str(), text.c_str());
}

Color ToColor(Vector3 c) {
  auto ch = [](float v) { return static_cast<unsigned char>(std::clamp(v, 0.0f, 1.0f) * 255.0f); };
  return {ch(c.x), ch(c.y), ch(c.z), 255};
}

void DrawOrientedBox(const Matrix& m, const BoundingBox& b, Color color) {
  Vector3 p[8];
  for (int i = 0; i < 8; ++i) {
    const Vector3 local{(i & 1) ? b.max.x : b.min.x, (i & 2) ? b.max.y : b.min.y,
                        (i & 4) ? b.max.z : b.min.z};
    p[i] = Vector3Transform(local, m);
  }
  for (int i = 0; i < 8; ++i) {
    for (int bit : {1, 2, 4}) {
      const int j = i ^ bit;
      if (i < j)
        DrawLine3D(p[i], p[j], color);
    }
  }
}

bool Contains(const std::vector<std::string>& v, const std::string& s) {
  return std::find(v.begin(), v.end(), s) != v.end();
}

Vector3 Snap(Vector3 v) {
  auto s = [](float x) { return std::round(x * 4.0f) / 4.0f; };
  return {s(v.x), s(v.y), s(v.z)};
}

std::string Trim(const std::string& s) {
  const auto isSpace = [](unsigned char c) { return std::isspace(c) != 0; };
  const auto first = std::find_if_not(s.begin(), s.end(), isSpace);
  const auto last = std::find_if_not(s.rbegin(), s.rend(), isSpace).base();
  return first < last ? std::string(first, last) : std::string();
}

std::string Sanitize(const std::string& s) {
  std::string out;
  for (char c : s) {
    const unsigned char u = static_cast<unsigned char>(c);
    if (std::isalnum(u) || c == '_' || c == '-')
      out += c;
    else if (c == ' ')
      out += '_';
  }
  return out;
}

} // namespace

// ---------------------------------------------------------------------------
// Ciclo de vida
// ---------------------------------------------------------------------------
EditorApp::EditorApp(retro::RetroEngine& engine) : engine_(engine) {
  rlImGuiSetup(true);

  ImGuiIO& io = ImGui::GetIO();
  io.ConfigFlags |= ImGuiConfigFlags_DockingEnable;
  io.ConfigWindowsMoveFromTitleBarOnly = true;

  iniPath_ = std::string(GetApplicationDirectory()) + "retro_editor.ini";
  io.IniFilename = iniPath_.c_str();
  layoutPending_ = !FileExists(iniPath_.c_str());

  const retro::RetroShaderParams& p = engine_.Retro().Params();
  defaults_ = {engine_.Config().clearColor, p.fogStart, p.fogEnd, p.lightDirection, p.ambient};

  Log(false, "Assets: " + engine_.Assets().Root());
  assets_.Scan(engine_.Assets().Root());
  RefreshSceneList();

  if (!sceneList_.empty()) {
    const auto demo = std::find(sceneList_.begin(), sceneList_.end(), "scenes/demo.json");
    OpenScene(demo != sceneList_.end() ? *demo : sceneList_.front(), true);
  } else {
    Log(true, "No se encontraron escenas en " + engine_.Assets().Root() + "scenes/");
  }
}

EditorApp::~EditorApp() { rlImGuiShutdown(); }

void EditorApp::Run() {
  while (!quit_) {
    if (WindowShouldClose() && !openUnsavedPopup_ && pending_.kind == PendingKind::None)
      RequestQuit();

    BeginDrawing();
    ClearBackground({30, 30, 34, 255});
    rlImGuiBegin();
    DrawUi();
    rlImGuiEnd();
    EndDrawing();

    UpdateTitle();
  }
}

void EditorApp::Log(bool error, std::string text) {
  log_.push_back({error, std::move(text)});
  if (log_.size() > 500)
    log_.erase(log_.begin(), log_.begin() + 100);
  logScroll_ = true;
}

// ---------------------------------------------------------------------------
// Escena y archivo
// ---------------------------------------------------------------------------
void EditorApp::RefreshSceneList() {
  sceneList_.clear();
  std::error_code ec;
  const fs::path dir = fs::path(engine_.Assets().Root()) / "scenes";
  for (const auto& entry : fs::directory_iterator(dir, ec)) {
    if (entry.is_regular_file() && entry.path().extension() == ".json") {
      sceneList_.push_back("scenes/" + entry.path().filename().string());
    }
  }
  std::sort(sceneList_.begin(), sceneList_.end());
}

void EditorApp::ResetEnvironment() {
  engine_.SetClearColor(defaults_.background);
  retro::RetroShaderParams p = engine_.Retro().Params();
  p.fogStart = defaults_.fogStart;
  p.fogEnd = defaults_.fogEnd;
  p.lightDirection = defaults_.lightDirection;
  p.ambient = defaults_.ambient;
  engine_.Retro().SetParams(p);
}

void EditorApp::ApplyViewMode() {
  retro::RetroShaderParams p = engine_.Retro().Params();
  if (ps1View_) {
    p.snapResolution = {static_cast<float>(engine_.Config().internalWidth),
                        static_cast<float>(engine_.Config().internalHeight)};
    p.colorLevels = 32.0f;
    p.ditherStrength = 1.0f;
  } else {
    p.snapResolution = {8192.0f, 8192.0f};
    p.colorLevels = 256.0f;
    p.ditherStrength = 0.0f;
  }
  p.fogEnabled = fogOn_;
  engine_.Retro().SetParams(p);
}

bool EditorApp::Rebuild(const Json& candidate, const std::string& path, std::string* error,
                        retro::RetroCamera* camera) {
  const retro::RetroShaderParams savedParams = engine_.Retro().Params();
  const Color savedClear = engine_.Config().clearColor;
  try {
    ResetEnvironment();
    auto next = std::make_unique<retro::Scene>();
    retro::RetroCamera localCamera;
    retro::LoadSceneFromJson(nlohmann::json(candidate), path, engine_, *next,
                             camera ? *camera : localCamera);

    for (std::size_t i = 0; i < next->Count(); ++i)
      next->EntityAt(i).visible = true;

    scene_ = std::move(next);
    engine_.Assets().PurgeUnused();
    ApplyViewMode();
    return true;
  } catch (const std::exception& e) {
    engine_.SetClearColor(savedClear);
    engine_.Retro().SetParams(savedParams);
    if (error)
      *error = e.what();
    return false;
  }
}

bool EditorApp::OpenScene(const std::string& relativePath, bool frameCamera) {
  try {
    std::ifstream file(engine_.Assets().Root() + relativePath);
    if (!file)
      throw std::runtime_error("no se pudo abrir el archivo");
    Json doc = Json::parse(file);

    engine_.Assets().Clear();
    thumbs_.clear();
    badAssets_.clear();
    assets_.Scan(engine_.Assets().Root());

    retro::RetroCamera sceneCamera;
    std::string error;
    if (!Rebuild(doc, relativePath, &error, &sceneCamera))
      throw std::runtime_error(error);

    const bool sameScene = relativePath == scenePath_;
    scenePath_ = relativePath;
    doc_ = doc;
    savedDoc_ = doc;
    lastGood_ = doc;
    undo_.clear();
    redo_.clear();
    gestureBase_.reset();
    needRebuild_ = false;
    lastUndoId_ = 0;

    if (frameCamera) {
      camera_.LookFrom(sceneCamera.Position(), sceneCamera.Target());
      camera_.fov = sceneCamera.Raw().fovy;
    }
    if (!sameScene || frameCamera)
      selection_ = {};
    ValidateSelection();
    UpdateDirty();
    Log(false, "Escena abierta: " + relativePath);
    return true;
  } catch (const std::exception& e) {
    Log(true, "No se pudo abrir " + relativePath + ": " + e.what());
    return false;
  }
}

bool EditorApp::Save() {
  if (scenePath_.empty())
    return false;
  try {
    std::string text = jsonfmt::FormatJson(doc_);
    try {
      if (Json::parse(text) != doc_)
        text = doc_.dump(2) + "\n";
    } catch (...) {
      text = doc_.dump(2) + "\n";
    }
    retro::savefile::WriteAtomic(fs::path(engine_.Assets().Root()) / scenePath_, text);
    savedDoc_ = doc_;
    UpdateDirty();
    Log(false, "Guardado: " + scenePath_);
    return true;
  } catch (const std::exception& e) {
    Log(true, std::string("No se pudo guardar: ") + e.what());
    return false;
  }
}

bool EditorApp::CreateScene(const std::string& rawName) {
  const std::string name = Sanitize(Trim(rawName));
  const bool hasLetter =
      std::any_of(name.begin(), name.end(), [](unsigned char c) { return std::isalnum(c) != 0; });
  if (!hasLetter) {
    Log(true, "Nombre de escena no valido: usa letras o numeros");
    return false;
  }
  const std::string relative = "scenes/" + name + ".json";
  const fs::path path = fs::path(engine_.Assets().Root()) / relative;
  std::error_code ec;
  if (fs::exists(path, ec)) {
    Log(true, "Ya existe " + relative);
    return false;
  }

  std::string groundTexture;
  for (const std::string& t : assets_.textures) {
    if (t.find("suelo") != std::string::npos) {
      groundTexture = t;
      break;
    }
  }
  if (groundTexture.empty() && !assets_.textures.empty())
    groundTexture = assets_.textures.front();

  Json ground = Json::object(
      {{"name", "suelo"},
       {"model",
        Json::object({{"type", "plane"}, {"size", 40}, {"subdivisions", 40}, {"tiles", 20}})}});
  if (!groundTexture.empty()) {
    ground["texture"] = groundTexture;
    ground["wrap"] = "repeat";
  }

  Json doc = Json::object();
  doc["camera"] = Json::object({{"position", Vec3Json({7.0f, 5.0f, 9.0f})},
                                {"target", Vec3Json({0.0f, 1.0f, 0.0f})},
                                {"fov", 60}});
  doc["player"] = Json::object({{"position", Vec3Json({0.0f, 0.0f, 9.0f})}, {"yaw", 0}});
  doc["autoCollide"] = true;
  doc["bounds"] =
      Json::object({{"min", Vec2Json({-19.0f, -19.0f})}, {"max", Vec2Json({19.0f, 19.0f})}});
  doc["entities"] = Json::array({ground});

  try {
    retro::savefile::WriteAtomic(path, jsonfmt::FormatJson(doc));
  } catch (const std::exception& e) {
    Log(true, std::string("No se pudo crear la escena: ") + e.what());
    return false;
  }
  RefreshSceneList();
  return OpenScene(relative, true);
}

void EditorApp::UpdateDirty() {
  dirty_ = doc_ != savedDoc_;
  UpdateTitle();
}

void EditorApp::UpdateTitle() {
  const std::string title = "RetroEditor - " +
                            (scenePath_.empty() ? std::string("(sin escena)") : scenePath_) +
                            (dirty_ ? " *" : "");
  if (title != windowTitle_) {
    windowTitle_ = title;
    SetWindowTitle(title.c_str());
  }
}

// ---------------------------------------------------------------------------
// Cambios y deshacer
// ---------------------------------------------------------------------------
// Protocolo: un widget que cambia algo llama a BeginChange(id) ANTES de tocar el JSON (guarda
// el documento previo) y a MarkChanged(id) DESPUES. Al final de cada panel, FlushChanges
// reconstruye la escena una sola vez: si falla, el cambio se revierte; si sale bien, se apunta
// en el historial. Los cambios seguidos del mismo campo (arrastrar un valor) se funden en una
// sola entrada de deshacer.
void EditorApp::BeginChange(ImGuiID) {
  if (!gestureBase_)
    gestureBase_ = doc_;
}

void EditorApp::MarkChanged(ImGuiID id) {
  needRebuild_ = true;
  gestureId_ = id;
}

void EditorApp::FlushChanges() {
  if (!needRebuild_) {
    if (gestureEnded_)
      lastUndoId_ = 0;
    gestureEnded_ = false;
    return;
  }
  needRebuild_ = false;

  std::string error;
  if (Rebuild(doc_, scenePath_, &error)) {
    const double now = ImGui::GetTime();
    const bool coalesce = gestureId_ != 0 && gestureId_ == lastUndoId_ &&
                          (now - lastUndoTime_) < 0.8 && !undo_.empty();
    if (!coalesce && gestureBase_ && *gestureBase_ != doc_) {
      undo_.push_back(std::move(*gestureBase_));
      if (undo_.size() > kMaxUndo)
        undo_.erase(undo_.begin());
    }
    redo_.clear();
    lastUndoId_ = gestureId_;
    lastUndoTime_ = now;
    lastGood_ = doc_;
  } else {
    Log(true, "Cambio descartado: " + error);
    doc_ = lastGood_;
  }
  gestureBase_.reset();
  gestureId_ = 0;
  if (gestureEnded_)
    lastUndoId_ = 0;
  gestureEnded_ = false;
  ValidateSelection();
  UpdateDirty();
}

bool EditorApp::ApplyDocument(const Json& doc) {
  std::string error;
  if (!Rebuild(doc, scenePath_, &error)) {
    Log(true, "No se pudo aplicar: " + error);
    return false;
  }
  doc_ = doc;
  lastGood_ = doc;
  lastUndoId_ = 0;
  gestureBase_.reset();
  ValidateSelection();
  UpdateDirty();
  return true;
}

void EditorApp::Undo() {
  if (undo_.empty() || ImGui::IsAnyItemActive())
    return;
  const Json current = doc_;
  if (!ApplyDocument(undo_.back()))
    return;
  undo_.pop_back();
  redo_.push_back(current);
}

void EditorApp::Redo() {
  if (redo_.empty() || ImGui::IsAnyItemActive())
    return;
  const Json current = doc_;
  if (!ApplyDocument(redo_.back()))
    return;
  redo_.pop_back();
  undo_.push_back(current);
}

void EditorApp::ValidateSelection() {
  if (selection_.kind != SelectionKind::None && !SelectedJson())
    selection_ = {};
}

const Json* EditorApp::SelectedJson() const {
  const char* key = KeyOf(selection_.kind);
  if (!key || selection_.index < 0 || !doc_.is_object() || !doc_.contains(key))
    return nullptr;
  const Json& array = doc_.at(key);
  if (!array.is_array() || selection_.index >= static_cast<int>(array.size()))
    return nullptr;
  return &array[static_cast<std::size_t>(selection_.index)];
}

Json* EditorApp::SelectedJsonMut() {
  return const_cast<Json*>(static_cast<const EditorApp*>(this)->SelectedJson());
}

// ---------------------------------------------------------------------------
// Geometria compartida por el picking, el enfoque y el gizmo
// ---------------------------------------------------------------------------
namespace {

constexpr float kPickRadius = 0.35f;

bool IsOneOf(const std::string& s, std::initializer_list<const char*> names) {
  for (const char* n : names) {
    if (s == n)
      return true;
  }
  return false;
}

Matrix EntityMatrix(retro::Entity& e) {
  if (!e.IsSprite())
    return e.transform.ToMatrix();
  const Vector3 p = e.transform.position;
  const float sx = std::fabs(e.transform.scale.x);
  const float sy = std::fabs(e.transform.scale.y);
  return MatrixMultiply(MatrixScale(sx, sy, sx), MatrixTranslate(p.x, p.y, p.z));
}

Matrix TriggerMatrix(const Json& t) {
  const Vector3 p = ReadVec3(&t, "position", {0.0f, 0.0f, 0.0f});
  const float yaw = ReadNumber(&t, "rotation", 0.0f);
  return MatrixMultiply(MatrixRotateY(yaw * DEG2RAD), MatrixTranslate(p.x, p.y, p.z));
}

BoundingBox TriggerLocalBox(const Json& t) {
  const Vector3 s = ReadVec3(&t, "size", {2.0f, 2.4f, 0.8f});
  return {{-s.x * 0.5f, 0.0f, -s.z * 0.5f}, {s.x * 0.5f, s.y, s.z * 0.5f}};
}

bool RayOrientedBox(const Ray& ray, const Matrix& m, BoundingBox box, float& distance) {
  constexpr float pad = 0.01f;
  box.min = {box.min.x - pad, box.min.y - pad, box.min.z - pad};
  box.max = {box.max.x + pad, box.max.y + pad, box.max.z + pad};

  const Matrix inv = MatrixInvert(m);
  const Vector3 o = Vector3Transform(ray.position, inv);
  const Vector3 t = Vector3Transform(Vector3Add(ray.position, ray.direction), inv);
  const Ray local{o, Vector3Subtract(t, o)};
  const RayCollision c = GetRayCollisionBox(local, box);
  if (!c.hit || !std::isfinite(c.distance) || c.distance < 0.0f)
    return false;
  distance = c.distance;
  return true;
}

void WorldBounds(const Matrix& m, const BoundingBox& b, Vector3& center, float& radius) {
  Vector3 lo{1e30f, 1e30f, 1e30f};
  Vector3 hi{-1e30f, -1e30f, -1e30f};
  for (int i = 0; i < 8; ++i) {
    const Vector3 local{(i & 1) ? b.max.x : b.min.x, (i & 2) ? b.max.y : b.min.y,
                        (i & 4) ? b.max.z : b.min.z};
    const Vector3 w = Vector3Transform(local, m);
    lo = {std::min(lo.x, w.x), std::min(lo.y, w.y), std::min(lo.z, w.z)};
    hi = {std::max(hi.x, w.x), std::max(hi.y, w.y), std::max(hi.z, w.z)};
  }
  center = Vector3Scale(Vector3Add(lo, hi), 0.5f);
  radius = Vector3Length(Vector3Subtract(hi, lo)) * 0.5f;
}

float RaySegmentDistance(const Ray& ray, Vector3 a, Vector3 b, float& s) {
  const Vector3 u = ray.direction;
  const Vector3 v = Vector3Subtract(b, a);
  const Vector3 w0 = Vector3Subtract(ray.position, a);
  const float uu = Vector3DotProduct(u, u);
  const float uv = Vector3DotProduct(u, v);
  const float vv = Vector3DotProduct(v, v);
  const float uw = Vector3DotProduct(u, w0);
  const float vw = Vector3DotProduct(v, w0);
  const float denom = uu * vv - uv * uv;

  float tc = denom > 1e-8f ? (uu * vw - uv * uw) / denom : (vv > 1e-8f ? vw / vv : 0.0f);
  tc = std::clamp(tc, 0.0f, 1.0f);
  const float sc = uu > 1e-8f ? std::max((uv * tc - uw) / uu, 0.0f) : 0.0f;

  s = tc;
  const Vector3 onRay = Vector3Add(ray.position, Vector3Scale(u, sc));
  const Vector3 onSeg = Vector3Add(a, Vector3Scale(v, tc));
  return Vector3Distance(onRay, onSeg);
}

bool AxisParameter(const Ray& ray, Vector3 origin, Vector3 axis, float& t) {
  const Vector3 u = ray.direction;
  const Vector3 w0 = Vector3Subtract(ray.position, origin);
  const float uu = Vector3DotProduct(u, u);
  const float uv = Vector3DotProduct(u, axis);
  const float vv = Vector3DotProduct(axis, axis);
  const float uw = Vector3DotProduct(u, w0);
  const float vw = Vector3DotProduct(axis, w0);
  const float denom = uu * vv - uv * uv;
  if (denom < 1e-6f)
    return false;
  t = (uu * vw - uv * uw) / denom;
  return true;
}

Vector3 AxisVector(int axis) {
  return {axis == 0 ? 1.0f : 0.0f, axis == 1 ? 1.0f : 0.0f, axis == 2 ? 1.0f : 0.0f};
}

Color AxisColor(int axis) {
  switch (axis) {
  case 0:
    return {235, 70, 70, 255};
  case 1:
    return {90, 215, 90, 255};
  default:
    return {80, 130, 245, 255};
  }
}

} // namespace

// ---------------------------------------------------------------------------
// Operaciones sobre el documento (anadir, duplicar, borrar, enfocar)
// ---------------------------------------------------------------------------
std::string EditorApp::UniqueName(const char* arrayKey, const std::string& base) const {
  auto taken = [&](const std::string& name) {
    if (!doc_.is_object() || !doc_.contains(arrayKey) || !doc_.at(arrayKey).is_array())
      return false;
    for (const Json& item : doc_.at(arrayKey)) {
      if (ReadString(&item, "name") == name)
        return true;
    }
    return false;
  };
  const std::string root = base.empty() ? std::string("objeto") : base;
  std::string candidate = root;
  for (int n = 2; taken(candidate); ++n)
    candidate = root + "_" + std::to_string(n);
  return candidate;
}

void EditorApp::InsertItem(SelectionKind kind, Json item) {
  const char* key = KeyOf(kind);
  if (!key || !doc_.is_object())
    return;

  BeginChange(0);
  if (!doc_.contains(key) || !doc_.at(key).is_array())
    doc_[key] = Json::array();
  doc_[key].push_back(std::move(item));
  selection_ = {kind, static_cast<int>(doc_.at(key).size()) - 1};
  MarkChanged(0);
}

void EditorApp::AddEntityModel(const std::string& model, Vector3 position) {
  const std::string stem = fs::path(model).stem().string();

  Json e = Json::object();
  e["name"] = UniqueName("entities", Sanitize(stem));
  e["model"] = model;

  for (const std::string& t : assets_.textures) {
    if (!stem.empty() && fs::path(t).stem().string().find(stem) != std::string::npos) {
      e["texture"] = t;
      break;
    }
  }
  if (!SameVec3(position, {0.0f, 0.0f, 0.0f}))
    e["position"] = Vec3Json(position);
  InsertItem(SelectionKind::Entity, std::move(e));
}

void EditorApp::AddEntitySprite(const std::string& texture, Vector3 position) {
  float width = 1.0f;
  if (auto tex = ThumbTexture(texture)) {
    const Texture2D& t = tex->Get();
    if (t.height > 0)
      width =
          std::round(2.0f * static_cast<float>(t.width) / static_cast<float>(t.height) * 100.0f) /
          100.0f;
  }

  Json e = Json::object();
  e["name"] = UniqueName("entities", Sanitize(fs::path(texture).stem().string()));
  e["sprite"] = texture;
  e["size"] = Vec2Json({width, 2.0f});
  if (!SameVec3(position, {0.0f, 0.0f, 0.0f}))
    e["position"] = Vec3Json(position);
  InsertItem(SelectionKind::Entity, std::move(e));
}

void EditorApp::AddTrigger(Vector3 position) {
  Json t = Json::object();
  t["name"] = UniqueName("triggers", "trigger");
  t["position"] = Vec3Json(position);
  t["size"] = Vec3Json({3.0f, 2.4f, 3.0f});
  t["do"] = Json::array();
  InsertItem(SelectionKind::Trigger, std::move(t));
}

void EditorApp::AddLight(Vector3 position) {
  position.y = std::max(position.y, 0.0f) + 2.0f;
  Json l = Json::object();
  l["position"] = Vec3Json(position);
  l["color"] = Json::array({255, 200, 120});
  l["radius"] = 8;
  l["intensity"] = 1;
  InsertItem(SelectionKind::Light, std::move(l));
}

void EditorApp::AddEmitter(const std::string& file, Vector3 position) {
  position.y = std::max(position.y, 0.0f) + 1.0f;
  Json e = Json::object();
  e["file"] = file;
  e["position"] = Vec3Json(position);
  e["volume"] = 1;
  e["radius"] = 10;
  InsertItem(SelectionKind::Emitter, std::move(e));
}

void EditorApp::DuplicateSelection() {
  const Json* source = SelectedJson();
  if (!source || !source->is_object())
    return;

  const SelectionKind kind = selection_.kind;
  Json copy = *source;
  if (copy.contains("name") && copy.at("name").is_string()) {
    copy["name"] = UniqueName(KeyOf(kind), copy.at("name").get<std::string>() + "_copia");
  }
  const Vector3 p = ReadVec3(&copy, "position", {0.0f, 0.0f, 0.0f});
  copy["position"] = Vec3Json({p.x + 1.0f, p.y, p.z});
  InsertItem(kind, std::move(copy));
}

void EditorApp::DeleteSelection() {
  const Json* item = SelectedJson();
  const char* key = KeyOf(selection_.kind);
  if (!item || !key)
    return;

  const std::string name = ReadString(item, "name");
  if (!name.empty()) {
    const std::string dump = doc_.dump();
    const std::string quoted = "\"" + name + "\"";
    std::size_t count = 0;
    for (std::size_t pos = dump.find(quoted); pos != std::string::npos;
         pos = dump.find(quoted, pos + 1))
      ++count;
    if (count > 1)
      Log(true,
          "Aviso: '" + name + "' se nombra en otros lugares de la escena (acciones o condiciones)");
  }

  BeginChange(0);
  Json& array = doc_[key];
  array.erase(array.begin() + selection_.index);
  if (array.empty() && selection_.kind != SelectionKind::Entity)
    doc_.erase(key);
  selection_ = {};
  MarkChanged(0);
}

bool EditorApp::SelectionCenter(Vector3& center, float& radius) const {
  const Json* item = SelectedJson();
  if (!item)
    return false;

  center = ReadVec3(item, "position", {0.0f, 0.0f, 0.0f});
  radius = 1.0f;

  if (selection_.kind == SelectionKind::Entity && scene_ &&
      static_cast<std::size_t>(selection_.index) < scene_->Count()) {
    retro::Entity& e = scene_->EntityAt(static_cast<std::size_t>(selection_.index));
    WorldBounds(EntityMatrix(e), e.LocalBounds(), center, radius);
  } else if (selection_.kind == SelectionKind::Trigger) {
    WorldBounds(TriggerMatrix(*item), TriggerLocalBox(*item), center, radius);
  }
  return true;
}

void EditorApp::FocusSelection() {
  Vector3 center{};
  float radius = 1.0f;
  if (!SelectionCenter(center, radius))
    return;
  camera_.FocusOn(center, std::max(radius * 2.5f, 3.0f));
}

void EditorApp::ApplyTextureToEntity(int index, const std::string& texture) {
  if (!doc_.is_object() || !doc_.contains("entities") || !doc_.at("entities").is_array())
    return;
  Json& list = doc_["entities"];
  if (index < 0 || index >= static_cast<int>(list.size()))
    return;

  Json& e = list[static_cast<std::size_t>(index)];
  BeginChange(0);
  e[e.contains("sprite") ? "sprite" : "texture"] = texture;
  selection_ = {SelectionKind::Entity, index};
  MarkChanged(0);
}

Vector3 EditorApp::GroundPoint(const Ray& ray) {
  if (ray.direction.y < -0.01f) {
    const float t = -ray.position.y / ray.direction.y;
    if (t > 0.0f && t < 60.0f)
      return Vector3Add(ray.position, Vector3Scale(ray.direction, t));
  }
  Vector3 p = Vector3Add(ray.position, Vector3Scale(ray.direction, 8.0f));
  p.y = 0.0f;
  return p;
}

Vector3 EditorApp::PlacementPoint() const {
  return Snap(GroundPoint(Ray{camera_.Position(), camera_.Forward()}));
}

void EditorApp::HandleAssetDrop(const std::string& path, Vector2 mouse) {
  Ray ray{};
  if (!MouseRay(mouse, ray))
    return;
  const Vector3 point = Snap(GroundPoint(ray));

  if (Contains(assets_.models, path)) {
    AddEntityModel(path, point);
  } else if (Contains(assets_.textures, path)) {
    for (const Hit& h : CollectHits(ray)) {
      if (h.kind == SelectionKind::Entity) {
        ApplyTextureToEntity(h.index, path);
        return;
      }
    }
    AddEntitySprite(path, point);
  } else if (Contains(assets_.sounds, path)) {
    AddEmitter(path, point);
  } else {
    Log(true, "Tipo de archivo no soportado: " + path);
  }
}

// ---------------------------------------------------------------------------
// Peticiones con confirmacion de cambios sin guardar
// ---------------------------------------------------------------------------
void EditorApp::RequestOpen(const std::string& path) {
  if (path.empty())
    return;
  if (dirty_) {
    pending_ = {PendingKind::OpenScene, path};
    openUnsavedPopup_ = true;
  } else {
    OpenScene(path, path != scenePath_);
  }
}

void EditorApp::RequestQuit() {
  if (dirty_) {
    pending_ = {PendingKind::Quit, {}};
    openUnsavedPopup_ = true;
  } else {
    quit_ = true;
  }
}

void EditorApp::RequestNewScene() {
  if (dirty_) {
    pending_ = {PendingKind::NewScene, {}};
    openUnsavedPopup_ = true;
  } else {
    openNewScenePopup_ = true;
  }
}

void EditorApp::ExecutePending() {
  const Pending p = pending_;
  pending_ = {};
  switch (p.kind) {
  case PendingKind::OpenScene:
    OpenScene(p.path, p.path != scenePath_);
    break;
  case PendingKind::Quit:
    quit_ = true;
    break;
  case PendingKind::NewScene:
    openNewScenePopup_ = true;
    break;
  case PendingKind::None:
    break;
  }
}

void EditorApp::ExecuteDeferred() {
  const Deferred what = deferred_;
  deferred_ = Deferred::None;
  if (what == Deferred::Duplicate)
    DuplicateSelection();
  if (what == Deferred::Delete)
    DeleteSelection();
}

// ---------------------------------------------------------------------------
// Interfaz: estructura
// ---------------------------------------------------------------------------
void EditorApp::DrawUi() {
  ExecuteDeferred();

  DrawMenuBar();

  const ImGuiID dock = ImGui::DockSpaceOverViewport(0, ImGui::GetMainViewport());
  if (layoutPending_) {
    BuildDefaultLayout(dock);
    layoutPending_ = false;
  }

  DrawHierarchy();
  DrawInspector();
  DrawAssets();
  DrawMessages();
  DrawViewport();
  DrawModals();
  HandleShortcuts();

  FlushChanges();
}

void EditorApp::DrawAddItems() {
  ImGui::BeginDisabled(scenePath_.empty());

  if (ImGui::BeginMenu("Entidad (modelo)", !assets_.models.empty())) {
    for (const std::string& m : assets_.models) {
      if (ImGui::MenuItem(m.c_str()))
        AddEntityModel(m, PlacementPoint());
    }
    ImGui::EndMenu();
  }
  if (ImGui::BeginMenu("Sprite", !assets_.textures.empty())) {
    for (const std::string& t : assets_.textures) {
      if (ImGui::MenuItem(t.c_str()))
        AddEntitySprite(t, PlacementPoint());
    }
    ImGui::EndMenu();
  }
  if (ImGui::MenuItem("Trigger"))
    AddTrigger(PlacementPoint());
  if (ImGui::MenuItem("Luz puntual"))
    AddLight(PlacementPoint());
  if (ImGui::BeginMenu("Sonido posicional", !assets_.sounds.empty())) {
    for (const std::string& s : assets_.sounds) {
      if (ImGui::MenuItem(s.c_str()))
        AddEmitter(s, PlacementPoint());
    }
    ImGui::EndMenu();
  }

  ImGui::EndDisabled();
}

void EditorApp::DrawMenuBar() {
  if (!ImGui::BeginMainMenuBar())
    return;
  const bool hasScene = !scenePath_.empty();

  if (ImGui::BeginMenu("Archivo")) {
    if (ImGui::MenuItem("Nueva escena..."))
      RequestNewScene();
    if (ImGui::BeginMenu("Abrir escena")) {
      RefreshSceneList();
      for (const std::string& s : sceneList_) {
        if (ImGui::MenuItem(s.c_str(), nullptr, s == scenePath_))
          RequestOpen(s);
      }
      ImGui::EndMenu();
    }
    ImGui::Separator();
    if (ImGui::MenuItem("Guardar", "Ctrl+S", false, hasScene))
      Save();
    if (ImGui::MenuItem("Recargar desde el disco", "F5", false, hasScene))
      RequestOpen(scenePath_);
    ImGui::Separator();
    if (ImGui::MenuItem("Salir"))
      RequestQuit();
    ImGui::EndMenu();
  }

  if (ImGui::BeginMenu("Editar")) {
    if (ImGui::MenuItem("Deshacer", "Ctrl+Z", false, !undo_.empty()))
      Undo();
    if (ImGui::MenuItem("Rehacer", "Ctrl+Y", false, !redo_.empty()))
      Redo();
    ImGui::Separator();
    const bool selected = SelectedJson() != nullptr;
    if (ImGui::MenuItem("Duplicar", "Ctrl+D", false, selected))
      deferred_ = Deferred::Duplicate;
    if (ImGui::MenuItem("Eliminar", "Supr", false, selected))
      deferred_ = Deferred::Delete;
    if (ImGui::MenuItem("Enfocar", "F", false, selected))
      FocusSelection();
    ImGui::EndMenu();
  }

  if (ImGui::BeginMenu("Anadir")) {
    DrawAddItems();
    ImGui::EndMenu();
  }

  if (ImGui::BeginMenu("Ver")) {
    if (ImGui::MenuItem("Aspecto PS1 (320x240 + acabado)", nullptr, &ps1View_))
      ApplyViewMode();
    if (ImGui::MenuItem("Niebla", nullptr, &fogOn_))
      ApplyViewMode();
    ImGui::MenuItem("Animar la escena", nullptr, &animate_);
    ImGui::Separator();
    ImGui::MenuItem("Rejilla", nullptr, &showGrid_);
    ImGui::MenuItem("Colisiones", nullptr, &showColliders_);
    ImGui::MenuItem("Zonas de trigger", nullptr, &showTriggers_);
    ImGui::MenuItem("Luces", nullptr, &showLights_);
    ImGui::MenuItem("Sonidos", nullptr, &showEmitters_);
    ImGui::MenuItem("Limites", nullptr, &showBounds_);
    ImGui::MenuItem("Punto de inicio", nullptr, &showSpawn_);
    ImGui::Separator();
    if (ImGui::MenuItem("Restablecer paneles"))
      layoutPending_ = true;
    ImGui::EndMenu();
  }

  if (ImGui::BeginMenu("Ayuda")) {
    ImGui::TextUnformatted("Visor:");
    ImGui::BulletText("Clic: seleccionar (clic repetido: el siguiente objeto en esa linea)");
    ImGui::BulletText("Arrastrar las flechas: mover (Ctrl: ajustar a 0.25)");
    ImGui::BulletText("Cuadrado central: mover sobre el suelo");
    ImGui::BulletText("Boton derecho + raton: mirar; W A S D mover, Q E bajar y subir");
    ImGui::BulletText("Mayus: mas rapido; rueda: acercar (o velocidad con el derecho)");
    ImGui::BulletText("Boton central + arrastrar: desplazar");
    ImGui::BulletText("Arrastrar desde Assets: modelo/sonido al suelo, textura a un objeto");
    ImGui::Separator();
    ImGui::TextUnformatted("Atajos: Ctrl+S guardar, Ctrl+Z/Y deshacer/rehacer,");
    ImGui::TextUnformatted("Ctrl+D duplicar, Supr eliminar, F enfocar, F5 recargar");
    ImGui::EndMenu();
  }

  ImGui::EndMainMenuBar();
}

void EditorApp::BuildDefaultLayout(ImGuiID dock) {
  ImGui::DockBuilderRemoveNode(dock);
  ImGui::DockBuilderAddNode(dock, ImGuiDockNodeFlags_DockSpace);
  ImGui::DockBuilderSetNodeSize(dock, ImGui::GetMainViewport()->Size);

  ImGuiID left = 0, rest = 0, right = 0, center = 0, bottom = 0, view = 0;
  ImGui::DockBuilderSplitNode(dock, ImGuiDir_Left, 0.20f, &left, &rest);
  ImGui::DockBuilderSplitNode(rest, ImGuiDir_Right, 0.27f, &right, &center);
  ImGui::DockBuilderSplitNode(center, ImGuiDir_Down, 0.25f, &bottom, &view);

  ImGui::DockBuilderDockWindow("Jerarquia", left);
  ImGui::DockBuilderDockWindow("Inspector", right);
  ImGui::DockBuilderDockWindow("Visor", view);
  ImGui::DockBuilderDockWindow("Assets", bottom);
  ImGui::DockBuilderDockWindow("Mensajes", bottom);
  ImGui::DockBuilderFinish(dock);
}

// ---------------------------------------------------------------------------
// Jerarquia
// ---------------------------------------------------------------------------
void EditorApp::DrawHierarchy() {
  if (!ImGui::Begin("Jerarquia")) {
    ImGui::End();
    return;
  }

  ImGui::TextColored(kAccent, "%s%s", scenePath_.empty() ? "(sin escena)" : scenePath_.c_str(),
                     dirty_ ? " *" : "");
  if (ImGui::Button("+ Anadir"))
    ImGui::OpenPopup("anadir_popup");
  if (ImGui::BeginPopup("anadir_popup")) {
    DrawAddItems();
    ImGui::EndPopup();
  }
  ImGui::Separator();

  struct Section {
    const char* title;
    const char* key;
    SelectionKind kind;
  };
  const Section sections[] = {{"Entidades", "entities", SelectionKind::Entity},
                              {"Triggers", "triggers", SelectionKind::Trigger},
                              {"Luces", "lights", SelectionKind::Light},
                              {"Sonidos", "emitters", SelectionKind::Emitter}};

  for (const Section& s : sections) {
    const Json* array = (doc_.is_object() && doc_.contains(s.key) && doc_.at(s.key).is_array())
                            ? &doc_.at(s.key)
                            : nullptr;
    const int count = array ? static_cast<int>(array->size()) : 0;

    // "###clave": el estado abierto/cerrado no se pierde cuando cambia el contador del titulo.
    const std::string header = std::string(s.title) + " (" + std::to_string(count) + ")###" + s.key;
    ImGui::SetNextItemOpen(true, ImGuiCond_Once);
    if (!ImGui::CollapsingHeader(header.c_str()))
      continue;

    ImGui::PushID(s.key);
    for (int i = 0; i < count; ++i) {
      const bool selected = selection_.kind == s.kind && selection_.index == i;
      const std::string label =
          ItemLabel(s.kind, i, (*array)[static_cast<std::size_t>(i)]) + "##" + std::to_string(i);
      if (ImGui::Selectable(label.c_str(), selected))
        selection_ = {s.kind, i};

      if (ImGui::BeginPopupContextItem()) {
        selection_ = {s.kind, i};
        if (ImGui::MenuItem("Duplicar", "Ctrl+D"))
          deferred_ = Deferred::Duplicate;
        if (ImGui::MenuItem("Eliminar", "Supr"))
          deferred_ = Deferred::Delete;
        if (ImGui::MenuItem("Enfocar", "F"))
          FocusSelection();
        ImGui::EndPopup();
      }
    }
    ImGui::PopID();
  }

  if (ImGui::IsWindowHovered() && ImGui::IsMouseClicked(0) && !ImGui::IsAnyItemHovered()) {
    selection_ = {};
  }
  ImGui::End();
}

// ---------------------------------------------------------------------------
// Inspector
// ---------------------------------------------------------------------------
void EditorApp::DrawInspector() {
  if (!ImGui::Begin("Inspector")) {
    ImGui::End();
    return;
  }
  ImGui::PushItemWidth(-125.0f);

  if (Json* item = SelectedJsonMut()) {
    ImGui::PushID(static_cast<int>(selection_.kind) * 100000 + selection_.index);

    ImGui::TextColored(kAccent, "%s", KindTitle(selection_.kind));
    ImGui::SameLine();
    if (ImGui::SmallButton("Duplicar"))
      deferred_ = Deferred::Duplicate;
    ImGui::SameLine();
    if (ImGui::SmallButton("Eliminar"))
      deferred_ = Deferred::Delete;
    ImGui::SameLine();
    if (ImGui::SmallButton("Enfocar"))
      FocusSelection();
    ImGui::Separator();

    switch (selection_.kind) {
    case SelectionKind::Entity:
      InspectEntity(*item);
      break;
    case SelectionKind::Trigger:
      InspectTrigger(*item);
      break;
    case SelectionKind::Light:
      InspectLight(*item);
      break;
    case SelectionKind::Emitter:
      InspectEmitter(*item);
      break;
    case SelectionKind::None:
      break;
    }
    ImGui::PopID();
  } else if (doc_.is_object()) {
    InspectScene();
  } else {
    ImGui::TextDisabled("Abre una escena desde el menu Archivo");
  }

  ImGui::PopItemWidth();
  ImGui::End();
}

void EditorApp::InspectEntity(Json& e) {
  const bool sprite = e.contains("sprite");

  FieldText("Nombre", e, nullptr, "name");
  FieldBool("Visible", e, nullptr, "visible", true);

  ImGui::SeparatorText(sprite ? "Sprite" : "Modelo");
  if (sprite) {
    FieldFile("Imagen", e, nullptr, "sprite", assets_.textures, false, true);
    FieldVec2("Tamano", e, nullptr, "size", {1.0f, 2.0f}, 0.05f, false);
    FieldBool("Emisivo", e, nullptr, "emissive", false);
  } else {
    if (e.contains("model") && e.at("model").is_object()) {
      ImGui::TextDisabled("Modelo primitivo: %s", ReadString(&e.at("model"), "type").c_str());
      FieldFloat("Tamano", e, "model", "size", 10.0f, 0.1f, 0.1f, 1000.0f, false);
      FieldInt("Subdivisiones", e, "model", "subdivisions", 1, 1, 256, false);
      FieldFloat("Repeticiones", e, "model", "tiles", 1.0f, 0.1f, 0.1f, 1000.0f, false);
    } else {
      FieldFile("Modelo", e, nullptr, "model", assets_.models, false, false);
    }
    FieldFile("Textura", e, nullptr, "texture", assets_.textures, true, true);
    if (e.contains("texture")) {
      FieldChoice("Repeticion", e, nullptr, "wrap", {"repeat", "clamp", "mirror"}, "");
    }
  }

  ImGui::SeparatorText("Transformacion");
  FieldVec3("Posicion", e, nullptr, "position", {0.0f, 0.0f, 0.0f}, 0.05f);
  if (!sprite)
    FieldVec3("Rotacion", e, nullptr, "rotation", {0.0f, 0.0f, 0.0f}, 0.5f);
  FieldVec3("Escala", e, nullptr, "scale", {1.0f, 1.0f, 1.0f}, 0.01f);
  FieldColor("Tinte", e, nullptr, "tint", WHITE, false);
  if (!sprite)
    FieldVec3("Giro (grados/s)", e, nullptr, "spin", {0.0f, 0.0f, 0.0f}, 0.5f);

  ImGui::SeparatorText("Colision");
  {
    const Json* c = e.contains("collider") ? &e.at("collider") : nullptr;
    const int mode = !c ? 0 : (c->is_boolean() ? (c->get<bool>() ? 1 : 2) : 3);
    if (mode == 3) {
      ImGui::TextDisabled("Caja personalizada (solo lectura)");
      ShowJsonValue("collider", *c);
    } else {
      const char* items[] = {"Segun la escena", "Si (caja automatica)", "No (atravesable)"};
      int next = mode;
      if (ImGui::Combo("Colision", &next, items, 3) && next != mode) {
        BeginChange(0);
        if (next == 0)
          e.erase("collider");
        else
          e["collider"] = (next == 1);
        MarkChanged(0);
      }
    }
  }

  ImGui::SeparatorText("Luz propia");
  if (!e.contains("light") || !e.at("light").is_object()) {
    if (ImGui::Button("Anadir luz")) {
      BeginChange(0);
      e["light"] = Json::object({{"offset", Vec3Json({0.0f, 2.0f, 0.0f})},
                                 {"color", Json::array({255, 200, 120})},
                                 {"radius", 8},
                                 {"intensity", 1}});
      MarkChanged(0);
    }
  } else {
    FieldVec3("Desplazamiento", e, "light", "offset", {0.0f, 0.0f, 0.0f}, 0.05f, false);
    FieldColor("Color", e, "light", "color", WHITE, false);
    FieldFloat("Intensidad", e, "light", "intensity", 1.0f, 0.01f, 0.0f, 10.0f, false);
    FieldFloat("Radio", e, "light", "radius", 8.0f, 0.1f, 0.1f, 100.0f, false);
    if (ImGui::Button("Quitar luz")) {
      BeginChange(0);
      e.erase("light");
      MarkChanged(0);
    }
  }

  ImGui::SeparatorText("Sonido propio");
  if (!e.contains("sound") || !e.at("sound").is_object()) {
    ImGui::BeginDisabled(assets_.sounds.empty());
    if (ImGui::Button("Anadir sonido")) {
      BeginChange(0);
      e["sound"] = Json::object({{"file", assets_.sounds.front()}, {"volume", 1}, {"radius", 10}});
      MarkChanged(0);
    }
    ImGui::EndDisabled();
  } else {
    FieldFile("Archivo", e, "sound", "file", assets_.sounds, false, false);
    FieldVec3("Desplazamiento", e, "sound", "offset", {0.0f, 0.0f, 0.0f}, 0.05f, false);
    FieldFloat("Volumen", e, "sound", "volume", 1.0f, 0.01f, 0.0f, 2.0f, false);
    FieldFloat("Radio", e, "sound", "radius", 10.0f, 0.1f, 0.1f, 200.0f, false);
    FieldFloat("Distancia minima", e, "sound", "minDistance", 1.0f, 0.05f, 0.0f, 100.0f, false);
    if (ImGui::Button("Quitar sonido")) {
      BeginChange(0);
      e.erase("sound");
      MarkChanged(0);
    }
  }

  bool header = false;
  for (auto it = e.begin(); it != e.end(); ++it) {
    if (IsOneOf(it.key(),
                {"name", "visible", "model", "sprite", "size", "emissive", "texture", "wrap",
                 "position", "rotation", "scale", "tint", "spin", "collider", "light", "sound"})) {
      continue;
    }
    if (!header) {
      ImGui::SeparatorText("Logica (solo lectura)");
      header = true;
    }
    ShowJsonValue(it.key(), it.value());
  }
}

void EditorApp::InspectTrigger(Json& t) {
  FieldText("Nombre", t, nullptr, "name");
  FieldVec3("Posicion", t, nullptr, "position", {0.0f, 0.0f, 0.0f}, 0.05f);
  FieldVec3("Tamano", t, nullptr, "size", {2.0f, 2.4f, 0.8f}, 0.05f);
  FieldFloat("Giro (grados)", t, nullptr, "rotation", 0.0f, 0.5f, -360.0f, 360.0f);
  FieldChoice("Evento", t, nullptr, "on", {"enter", "exit", "stay"}, "enter");
  FieldBool("Una sola vez", t, nullptr, "once", false);
  FieldBool("Dibujar marco", t, nullptr, "marker", false);
  if (ReadBool(&t, "marker", false))
    FieldColor("Color del marco", t, nullptr, "color", {255, 200, 80, 255}, false);

  bool header = false;
  for (auto it = t.begin(); it != t.end(); ++it) {
    if (IsOneOf(it.key(),
                {"name", "position", "size", "rotation", "on", "once", "marker", "color"}))
      continue;
    if (!header) {
      ImGui::SeparatorText("Condiciones y acciones (solo lectura)");
      header = true;
    }
    ShowJsonValue(it.key(), it.value());
  }
}

void EditorApp::InspectLight(Json& l) {
  FieldVec3("Posicion", l, nullptr, "position", {0.0f, 0.0f, 0.0f}, 0.05f);
  FieldColor("Color", l, nullptr, "color", WHITE, false);
  FieldFloat("Intensidad", l, nullptr, "intensity", 1.0f, 0.01f, 0.0f, 10.0f);
  FieldFloat("Radio", l, nullptr, "radius", 8.0f, 0.1f, 0.1f, 100.0f);
}

void EditorApp::InspectEmitter(Json& e) {
  FieldFile("Archivo", e, nullptr, "file", assets_.sounds, false, false);
  FieldVec3("Posicion", e, nullptr, "position", {0.0f, 0.0f, 0.0f}, 0.05f);
  FieldFloat("Volumen", e, nullptr, "volume", 1.0f, 0.01f, 0.0f, 2.0f);
  FieldFloat("Radio", e, nullptr, "radius", 10.0f, 0.1f, 0.1f, 200.0f);
  FieldFloat("Distancia minima", e, nullptr, "minDistance", 1.0f, 0.05f, 0.0f, 100.0f);
}

void EditorApp::InspectScene() {
  ImGui::TextColored(kAccent, "Ajustes de la escena");
  ImGui::TextDisabled("Selecciona un objeto para editarlo");
  ImGui::Separator();

  Json& d = doc_;

  ImGui::SeparatorText("Camara inicial");
  FieldVec3("Posicion", d, "camera", "position", {7.0f, 5.0f, 9.0f}, 0.1f, false);
  FieldVec3("Mira hacia", d, "camera", "target", {0.0f, 1.0f, 0.0f}, 0.1f, false);
  FieldFloat("Campo de vision", d, "camera", "fov", 60.0f, 0.5f, 20.0f, 120.0f, false);
  if (ImGui::Button("Usar la vista actual")) {
    const Vector3 p = camera_.Position();
    const Vector3 t = Vector3Add(p, Vector3Scale(camera_.Forward(), 6.0f));
    BeginChange(0);
    Store(d, "camera", "position", Vec3Json(p), false);
    Store(d, "camera", "target", Vec3Json(t), false);
    Store(d, "camera", "fov", Number(camera_.fov), false);
    MarkChanged(0);
  }

  ImGui::SeparatorText("Jugador");
  FieldVec3("Inicio", d, "player", "position", {0.0f, 0.0f, 0.0f}, 0.1f, false);
  FieldFloat("Giro inicial", d, "player", "yaw", 0.0f, 0.5f, -360.0f, 360.0f, false);
  if (ImGui::Button("Inicio en el centro de la vista")) {
    const Vector3 p = PlacementPoint();
    BeginChange(0);
    Store(d, "player", "position", Vec3Json(p), false);
    MarkChanged(0);
  }

  ImGui::SeparatorText("Mundo");
  FieldBool("Colision automatica", d, nullptr, "autoCollide", false, false);
  FieldColor("Fondo", d, nullptr, "background", defaults_.background, false);
  FieldFloat("Niebla: inicio", d, "fog", "start", defaults_.fogStart, 0.1f, 0.0f, 500.0f);
  FieldFloat("Niebla: fin", d, "fog", "end", defaults_.fogEnd, 0.1f, 0.1f, 500.0f);
  FieldVec3("Luz: direccion", d, "light", "direction", defaults_.lightDirection, 0.01f);
  FieldFloat("Luz: ambiente", d, "light", "ambient", defaults_.ambient, 0.005f, 0.0f, 1.0f);

  ImGui::SeparatorText("Limites jugables");
  if (!d.contains("bounds") || !d.at("bounds").is_object()) {
    if (ImGui::Button("Activar limites")) {
      BeginChange(0);
      d["bounds"] =
          Json::object({{"min", Vec2Json({-19.0f, -19.0f})}, {"max", Vec2Json({19.0f, 19.0f})}});
      MarkChanged(0);
    }
  } else {
    FieldVec2("Minimo (X, Z)", d, "bounds", "min", {-19.0f, -19.0f}, 0.1f, false);
    FieldVec2("Maximo (X, Z)", d, "bounds", "max", {19.0f, 19.0f}, 0.1f, false);
    if (ImGui::Button("Quitar limites")) {
      BeginChange(0);
      d.erase("bounds");
      MarkChanged(0);
    }
  }

  bool header = false;
  for (auto it = d.begin(); it != d.end(); ++it) {
    if (IsOneOf(it.key(), {"camera", "player", "autoCollide", "bounds", "background", "fog",
                           "light", "entities", "triggers", "lights", "emitters"})) {
      continue;
    }
    if (!header) {
      ImGui::SeparatorText("Otros (solo lectura)");
      header = true;
    }
    ShowJsonValue(it.key(), it.value());
  }
}

// ---------------------------------------------------------------------------
// Assets
// ---------------------------------------------------------------------------
std::shared_ptr<retro::TextureResource> EditorApp::ThumbTexture(const std::string& relative) {
  const auto it = thumbs_.find(relative);
  if (it != thumbs_.end())
    return it->second;
  if (badAssets_.count(relative) != 0)
    return nullptr;
  try {
    auto texture = engine_.Assets().GetTexture(relative);
    thumbs_[relative] = texture;
    return texture;
  } catch (const std::exception& e) {
    badAssets_.insert(relative);
    Log(true, "No se pudo cargar " + relative + ": " + e.what());
    return nullptr;
  }
}

void EditorApp::Thumbnail(const std::string& relative, float maxSize) {
  const auto texture = ThumbTexture(relative);
  if (!texture) {
    ImGui::TextColored(kError, "(no se pudo cargar)");
    return;
  }
  const Texture2D& t = texture->Get();
  if (t.width <= 0 || t.height <= 0)
    return;
  const float k =
      std::min(maxSize / static_cast<float>(t.width), maxSize / static_cast<float>(t.height));
  rlImGuiImageSize(&t, std::max(1, static_cast<int>(static_cast<float>(t.width) * k)),
                   std::max(1, static_cast<int>(static_cast<float>(t.height) * k)));
}

void EditorApp::AssetList(const std::vector<std::string>& files, bool thumbnails) {
  if (files.empty()) {
    ImGui::TextDisabled("(no hay archivos)");
    return;
  }

  const float cell = 76.0f;
  const float spacing = ImGui::GetStyle().ItemSpacing.x;
  const float width = ImGui::GetContentRegionAvail().x;
  const int perRow = std::max(1, static_cast<int>((width + spacing) / (cell + spacing)));
  int column = 0;

  for (const std::string& file : files) {
    ImGui::PushID(file.c_str());
    const std::string shortName = fs::path(file).filename().string();
    bool activated = false;

    if (thumbnails) {
      ImGui::BeginGroup();
      const auto texture = ThumbTexture(file);
      if (texture) {
        const Texture2D& t = texture->Get();
        const float k = t.width > 0 && t.height > 0 ? std::min(64.0f / static_cast<float>(t.width),
                                                               64.0f / static_cast<float>(t.height))
                                                    : 1.0f;
        activated = rlImGuiImageButtonSize("##thumb", &t,
                                           {std::max(8.0f, static_cast<float>(t.width) * k),
                                            std::max(8.0f, static_cast<float>(t.height) * k)});
      } else {
        activated = ImGui::Button("?", ImVec2(64.0f, 64.0f));
      }
      const bool hovered = ImGui::IsItemHovered();

      if (ImGui::BeginDragDropSource()) {
        ImGui::SetDragDropPayload("ASSET", file.c_str(), file.size() + 1);
        ImGui::TextUnformatted(file.c_str());
        ImGui::EndDragDropSource();
      }
      if (hovered) {
        ImGui::SetTooltip(
            "%s\nDoble clic: aplicar a la entidad seleccionada\nArrastrar: al visor o a un campo",
            file.c_str());
      }
      ImGui::PushTextWrapPos(ImGui::GetCursorPosX() + cell);
      ImGui::TextUnformatted(shortName.c_str());
      ImGui::PopTextWrapPos();
      ImGui::EndGroup();

      if (hovered && ImGui::IsMouseDoubleClicked(0) && selection_.kind == SelectionKind::Entity) {
        ApplyTextureToEntity(selection_.index, file);
      }
      if (++column < perRow)
        ImGui::SameLine();
      else
        column = 0;
    } else {
      activated = ImGui::Selectable(file.c_str());
      if (ImGui::BeginDragDropSource()) {
        ImGui::SetDragDropPayload("ASSET", file.c_str(), file.size() + 1);
        ImGui::TextUnformatted(file.c_str());
        ImGui::EndDragDropSource();
      }
      if (ImGui::IsItemHovered() && ImGui::IsMouseDoubleClicked(0)) {
        if (Contains(assets_.models, file))
          AddEntityModel(file, PlacementPoint());
        else if (Contains(assets_.sounds, file))
          AddEmitter(file, PlacementPoint());
      }
    }
    (void)activated;
    ImGui::PopID();
  }
}

void EditorApp::DrawAssets() {
  if (!ImGui::Begin("Assets")) {
    ImGui::End();
    return;
  }

  if (ImGui::SmallButton("Volver a leer la carpeta")) {
    assets_.Scan(engine_.Assets().Root());
    thumbs_.clear();
    badAssets_.clear();
    Log(false, "Assets: " + std::to_string(assets_.models.size()) + " modelos, " +
                   std::to_string(assets_.textures.size()) + " texturas, " +
                   std::to_string(assets_.sounds.size()) + " sonidos");
  }
  ImGui::SameLine();
  ImGui::TextDisabled("Arrastra al visor, o doble clic");

  if (ImGui::BeginTabBar("assets_tabs")) {
    if (ImGui::BeginTabItem("Modelos")) {
      ImGui::BeginChild("lista_modelos");
      AssetList(assets_.models, false);
      ImGui::EndChild();
      ImGui::EndTabItem();
    }
    if (ImGui::BeginTabItem("Texturas")) {
      ImGui::BeginChild("lista_texturas");
      AssetList(assets_.textures, true);
      ImGui::EndChild();
      ImGui::EndTabItem();
    }
    if (ImGui::BeginTabItem("Sonidos")) {
      ImGui::BeginChild("lista_sonidos");
      AssetList(assets_.sounds, false);
      ImGui::EndChild();
      ImGui::EndTabItem();
    }
    ImGui::EndTabBar();
  }
  ImGui::End();
}

// ---------------------------------------------------------------------------
// Mensajes, dialogos y atajos
// ---------------------------------------------------------------------------
void EditorApp::DrawMessages() {
  if (!ImGui::Begin("Mensajes")) {
    ImGui::End();
    return;
  }
  if (ImGui::SmallButton("Limpiar"))
    log_.clear();
  ImGui::Separator();

  ImGui::BeginChild("mensajes_scroll");
  for (const LogLine& line : log_) {
    if (line.error) {
      ImGui::PushStyleColor(ImGuiCol_Text, kError);
      ImGui::TextWrapped("%s", line.text.c_str());
      ImGui::PopStyleColor();
    } else {
      ImGui::TextWrapped("%s", line.text.c_str());
    }
  }
  if (logScroll_) {
    ImGui::SetScrollHereY(1.0f);
    logScroll_ = false;
  }
  ImGui::EndChild();
  ImGui::End();
}

void EditorApp::DrawModals() {
  if (openUnsavedPopup_) {
    ImGui::OpenPopup("Cambios sin guardar");
    openUnsavedPopup_ = false;
  }
  if (ImGui::BeginPopupModal("Cambios sin guardar", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
    ImGui::Text("La escena \"%s\" tiene cambios sin guardar.", scenePath_.c_str());
    ImGui::Spacing();
    if (ImGui::Button("Guardar", ImVec2(110.0f, 0.0f))) {
      if (Save()) {
        ImGui::CloseCurrentPopup();
        ExecutePending();
      }
    }
    ImGui::SameLine();
    if (ImGui::Button("Descartar", ImVec2(110.0f, 0.0f))) {
      ImGui::CloseCurrentPopup();
      ExecutePending();
    }
    ImGui::SameLine();
    if (ImGui::Button("Cancelar", ImVec2(110.0f, 0.0f))) {
      pending_ = {};
      ImGui::CloseCurrentPopup();
    }
    ImGui::EndPopup();
  }

  if (openNewScenePopup_) {
    newSceneName_[0] = '\0';
    ImGui::OpenPopup("Nueva escena");
    openNewScenePopup_ = false;
  }
  if (ImGui::BeginPopupModal("Nueva escena", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
    ImGui::TextUnformatted("Nombre (letras, numeros, - y _):");
    if (ImGui::IsWindowAppearing())
      ImGui::SetKeyboardFocusHere();
    const bool enter = ImGui::InputText("##nombre", newSceneName_, sizeof newSceneName_,
                                        ImGuiInputTextFlags_EnterReturnsTrue);
    ImGui::Spacing();
    if (ImGui::Button("Crear", ImVec2(110.0f, 0.0f)) || enter) {
      if (CreateScene(newSceneName_))
        ImGui::CloseCurrentPopup();
    }
    ImGui::SameLine();
    if (ImGui::Button("Cancelar", ImVec2(110.0f, 0.0f)))
      ImGui::CloseCurrentPopup();
    ImGui::EndPopup();
  }
}

void EditorApp::HandleShortcuts() {
  ImGuiIO& io = ImGui::GetIO();
  if (io.WantTextInput)
    return;

  const bool ctrl = io.KeyCtrl;
  const bool shift = io.KeyShift;

  if (ctrl && ImGui::IsKeyPressed(ImGuiKey_S, false))
    Save();
  if (ctrl && ImGui::IsKeyPressed(ImGuiKey_Z)) {
    if (shift)
      Redo();
    else
      Undo();
  }
  if (ctrl && ImGui::IsKeyPressed(ImGuiKey_Y))
    Redo();
  if (ctrl && ImGui::IsKeyPressed(ImGuiKey_D, false))
    deferred_ = Deferred::Duplicate;
  if (!ctrl && ImGui::IsKeyPressed(ImGuiKey_Delete, false))
    deferred_ = Deferred::Delete;
  if (!ctrl && !lookingNow_ && ImGui::IsKeyPressed(ImGuiKey_F, false))
    FocusSelection();
  if (ImGui::IsKeyPressed(ImGuiKey_F5, false) && !scenePath_.empty())
    RequestOpen(scenePath_);
}

// ---------------------------------------------------------------------------
// Campos editables
// ---------------------------------------------------------------------------
// Cada campo lee su valor del JSON, lo muestra, y si el usuario lo cambia sigue el protocolo
// BeginChange / escribir / MarkChanged. Con eraseDefault, un valor igual al de por defecto
// se quita del archivo en vez de escribirse.
namespace {

// Dos niveles de PushID (padre y clave): el mismo nombre en "light" y en "sound" no choca.
struct IdScope {
  IdScope(const char* parent, const char* key) {
    ImGui::PushID(parent ? parent : "raiz");
    ImGui::PushID(key);
  }
  ~IdScope() {
    ImGui::PopID();
    ImGui::PopID();
  }
  IdScope(const IdScope&) = delete;
  IdScope& operator=(const IdScope&) = delete;
};

unsigned char Channel(float v) {
  return static_cast<unsigned char>(
      std::clamp(static_cast<long>(std::lround(v * 255.0f)), 0L, 255L));
}

} // namespace

bool EditorApp::FieldVec3(const char* label, Json& root, const char* parent, const char* key,
                          Vector3 def, float speed, bool eraseDefault) {
  IdScope scope(parent, key);
  const Vector3 current = ReadVec3(Src(root, parent), key, def);
  float v[3] = {current.x, current.y, current.z};
  const ImGuiID id = ImGui::GetID("w");
  const bool changed = ImGui::DragFloat3(label, v, speed, 0.0f, 0.0f, "%.2f");
  if (ImGui::IsItemDeactivated())
    gestureEnded_ = true;
  if (!changed)
    return false;

  const Vector3 next{v[0], v[1], v[2]};
  BeginChange(id);
  Store(root, parent, key, Vec3Json(next), eraseDefault && SameVec3(next, def));
  MarkChanged(id);
  return true;
}

bool EditorApp::FieldVec2(const char* label, Json& root, const char* parent, const char* key,
                          Vector2 def, float speed, bool eraseDefault) {
  IdScope scope(parent, key);
  const Vector2 current = ReadVec2(Src(root, parent), key, def);
  float v[2] = {current.x, current.y};
  const ImGuiID id = ImGui::GetID("w");
  const bool changed = ImGui::DragFloat2(label, v, speed, 0.0f, 0.0f, "%.2f");
  if (ImGui::IsItemDeactivated())
    gestureEnded_ = true;
  if (!changed)
    return false;

  const Vector2 next{v[0], v[1]};
  BeginChange(id);
  Store(root, parent, key, Vec2Json(next), eraseDefault && SameVec2(next, def));
  MarkChanged(id);
  return true;
}

bool EditorApp::FieldFloat(const char* label, Json& root, const char* parent, const char* key,
                           float def, float speed, float minValue, float maxValue,
                           bool eraseDefault) {
  IdScope scope(parent, key);
  float v = ReadNumber(Src(root, parent), key, def);
  const ImGuiID id = ImGui::GetID("w");
  const ImGuiSliderFlags flags =
      minValue < maxValue ? ImGuiSliderFlags_AlwaysClamp : ImGuiSliderFlags_None;
  const bool changed = ImGui::DragFloat(label, &v, speed, minValue, maxValue, "%.2f", flags);
  if (ImGui::IsItemDeactivated())
    gestureEnded_ = true;
  if (!changed)
    return false;

  BeginChange(id);
  Store(root, parent, key, Number(v), eraseDefault && SameNumber(v, def));
  MarkChanged(id);
  return true;
}

bool EditorApp::FieldInt(const char* label, Json& root, const char* parent, const char* key,
                         int def, int minValue, int maxValue, bool eraseDefault) {
  IdScope scope(parent, key);
  int v = def;
  if (const Json* src = Src(root, parent);
      src && src->is_object() && src->contains(key) && src->at(key).is_number()) {
    v = src->at(key).get<int>();
  }
  const ImGuiID id = ImGui::GetID("w");
  const ImGuiSliderFlags flags =
      minValue < maxValue ? ImGuiSliderFlags_AlwaysClamp : ImGuiSliderFlags_None;
  const bool changed = ImGui::DragInt(label, &v, 0.2f, minValue, maxValue, "%d", flags);
  if (ImGui::IsItemDeactivated())
    gestureEnded_ = true;
  if (!changed)
    return false;

  BeginChange(id);
  Store(root, parent, key, Json(v), eraseDefault && v == def);
  MarkChanged(id);
  return true;
}

bool EditorApp::FieldBool(const char* label, Json& root, const char* parent, const char* key,
                          bool def, bool eraseDefault) {
  IdScope scope(parent, key);
  bool v = ReadBool(Src(root, parent), key, def);
  if (!ImGui::Checkbox(label, &v))
    return false;

  BeginChange(0);
  Store(root, parent, key, Json(v), eraseDefault && v == def);
  MarkChanged(0);
  return true;
}

bool EditorApp::FieldColor(const char* label, Json& root, const char* parent, const char* key,
                           Color def, bool alpha) {
  IdScope scope(parent, key);
  const Color current = ReadColor(Src(root, parent), key, def);
  float c[4] = {static_cast<float>(current.r) / 255.0f, static_cast<float>(current.g) / 255.0f,
                static_cast<float>(current.b) / 255.0f, static_cast<float>(current.a) / 255.0f};
  const ImGuiID id = ImGui::GetID("w");
  const ImGuiColorEditFlags flags = ImGuiColorEditFlags_Uint8 | ImGuiColorEditFlags_DisplayRGB;
  const bool changed =
      alpha ? ImGui::ColorEdit4(label, c, flags) : ImGui::ColorEdit3(label, c, flags);
  if (ImGui::IsItemDeactivated())
    gestureEnded_ = true;
  if (!changed)
    return false;

  const Color next{Channel(c[0]), Channel(c[1]), Channel(c[2]), alpha ? Channel(c[3]) : current.a};
  const bool isDefault = next.r == def.r && next.g == def.g && next.b == def.b && next.a == def.a;
  const Json value = (alpha || next.a != 255)
                         ? Json::array({static_cast<int>(next.r), static_cast<int>(next.g),
                                        static_cast<int>(next.b), static_cast<int>(next.a)})
                         : Json::array({static_cast<int>(next.r), static_cast<int>(next.g),
                                        static_cast<int>(next.b)});
  BeginChange(id);
  Store(root, parent, key, value, isDefault);
  MarkChanged(id);
  return true;
}

bool EditorApp::FieldFile(const char* label, Json& root, const char* parent, const char* key,
                          const std::vector<std::string>& files, bool allowNone, bool thumbnail) {
  IdScope scope(parent, key);
  const std::string current = ReadString(Src(root, parent), key);

  if (thumbnail && !current.empty())
    Thumbnail(current, 56.0f);

  const bool known = current.empty() || Contains(files, current);
  std::string shown = current.empty() ? std::string("(ninguno)") : current;
  if (!known)
    shown += "  (no esta en assets)";

  bool changed = false;
  bool erase = false;
  std::string chosen;

  if (!known)
    ImGui::PushStyleColor(ImGuiCol_Text, kError);
  const bool open = ImGui::BeginCombo(label, shown.c_str());
  if (!known)
    ImGui::PopStyleColor();
  if (open) {
    if (allowNone && ImGui::Selectable("(ninguno)", current.empty())) {
      changed = true;
      erase = true;
    }
    for (const std::string& f : files) {
      const bool selected = f == current;
      if (ImGui::Selectable(f.c_str(), selected)) {
        changed = true;
        erase = false;
        chosen = f;
      }
      if (selected)
        ImGui::SetItemDefaultFocus();
    }
    ImGui::EndCombo();
  }

  if (ImGui::BeginDragDropTarget()) {
    if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload("ASSET")) {
      const std::string dropped(static_cast<const char*>(payload->Data));
      if (Contains(files, dropped)) {
        changed = true;
        erase = false;
        chosen = dropped;
      }
    }
    ImGui::EndDragDropTarget();
  }

  if (!changed)
    return false;
  if (erase ? current.empty() : chosen == current)
    return false;

  BeginChange(0);
  Store(root, parent, key, Json(chosen), erase);
  MarkChanged(0);
  return true;
}

bool EditorApp::FieldText(const char* label, Json& root, const char* parent, const char* key) {
  IdScope scope(parent, key);
  const std::string current = ReadString(Src(root, parent), key);
  const ImGuiID id = ImGui::GetID("w");
  const std::string sid = std::to_string(id);
  const bool editing = textId_ == sid;

  char buffer[sizeof textBuf_];
  std::snprintf(buffer, sizeof buffer, "%s", (editing ? std::string(textBuf_) : current).c_str());
  if (ImGui::InputText(label, buffer, sizeof buffer)) {
    textId_ = sid;
    std::snprintf(textBuf_, sizeof textBuf_, "%s", buffer);
  }

  if (ImGui::IsItemDeactivatedAfterEdit()) {
    const std::string value = textBuf_;
    textId_.clear();
    if (value == current)
      return false;
    BeginChange(0);
    Store(root, parent, key, Json(value), value.empty());
    MarkChanged(0);
    return true;
  }
  if (editing && !ImGui::IsItemActive())
    textId_.clear();
  return false;
}

bool EditorApp::FieldChoice(const char* label, Json& root, const char* parent, const char* key,
                            const std::vector<std::string>& options, const std::string& def) {
  IdScope scope(parent, key);
  const std::string current = ReadString(Src(root, parent), key);
  const std::string effective = current.empty() ? def : current;
  const std::string shown = effective.empty() ? std::string("(por defecto)") : effective;

  bool changed = false;
  bool erase = false;
  std::string chosen;

  if (ImGui::BeginCombo(label, shown.c_str())) {
    if (def.empty() && ImGui::Selectable("(por defecto)", current.empty())) {
      changed = true;
      erase = true;
    }
    for (const std::string& o : options) {
      if (ImGui::Selectable(o.c_str(), o == effective)) {
        changed = true;
        erase = !def.empty() && o == def;
        chosen = o;
      }
    }
    ImGui::EndCombo();
  }

  if (!changed)
    return false;
  if (erase ? current.empty() : chosen == current)
    return false;

  BeginChange(0);
  Store(root, parent, key, Json(chosen), erase);
  MarkChanged(0);
  return true;
}

// ---------------------------------------------------------------------------
// Visor
// ---------------------------------------------------------------------------
void EditorApp::EnsureTargets(int w, int h) {
  if (!viewTarget_ || viewTarget_->Width() != w || viewTarget_->Height() != h) {
    viewTarget_ = std::make_unique<retro::RenderTarget>(w, h);
    const Texture2D& tex = viewTarget_->Get().texture;
    SetTextureFilter(tex, TEXTURE_FILTER_POINT);
    SetTextureWrap(tex, TEXTURE_WRAP_REPEAT);
  }
}

void EditorApp::DrawViewport() {
  ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0f, 0.0f));
  const bool open = ImGui::Begin("Visor", nullptr,
                                 ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);
  ImGui::PopStyleVar();
  if (!open) {
    viewportHovered_ = false;
    ImGui::End();
    return;
  }

  if (!scene_) {
    ImGui::TextDisabled("Abre una escena desde el menu Archivo");
    viewportHovered_ = false;
    ImGui::End();
    return;
  }

  const ImVec2 avail = ImGui::GetContentRegionAvail();
  const int w = std::max(16, static_cast<int>(avail.x));
  const int h = std::max(16, static_cast<int>(avail.y));

  const ImVec2 origin = ImGui::GetCursorScreenPos();
  const ImVec2 mp = ImGui::GetIO().MousePos;
  const Vector2 mouse{mp.x - origin.x, mp.y - origin.y};

  lookingNow_ = camera_.Update(GetFrameTime(), viewportHovered_);
  EnsureTargets(w, h);
  viewW_ = w;
  viewH_ = h;

  HandleViewportMouse(mouse);
  RenderViewport(w, h);
  rlImGuiImageRenderTexture(&viewTarget_->Get());
  viewportHovered_ = ImGui::IsItemHovered();

  if (ImGui::BeginDragDropTarget()) {
    if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload("ASSET")) {
      HandleAssetDrop(std::string(static_cast<const char*>(payload->Data)), mouse);
    }
    ImGui::EndDragDropTarget();
  }

  ImGui::SetCursorPos(ImVec2(8.0f, 8.0f));
  ImGui::TextColored(ImVec4(1, 1, 1, 0.75f), "%s%s   %d FPS%s", scenePath_.c_str(),
                     dirty_ ? " *" : "", GetFPS(), ps1View_ ? "   [vista PS1]" : "");
  ImGui::End();
}

void EditorApp::RenderViewport(int w, int h) {
  const Color clear = engine_.Config().clearColor;
  const Camera3D cam = camera_.Raw();
  if (animate_)
    scene_->Update(GetFrameTime(), camera_.Position());

  if (ps1View_) {
    const int iw = engine_.Config().internalWidth;
    const int ih = engine_.Config().internalHeight;
    if (!sceneTarget_ || sceneTarget_->Width() != iw || sceneTarget_->Height() != ih) {
      sceneTarget_ = std::make_unique<retro::RenderTarget>(iw, ih);
    }

    BeginTextureMode(sceneTarget_->Get());
    ClearBackground(clear);
    BeginMode3D(cam);
    DrawWorld(cam);
    EndMode3D();
    EndTextureMode();

    const float scale = std::min(static_cast<float>(w) / static_cast<float>(iw),
                                 static_cast<float>(h) / static_cast<float>(ih));
    const Rectangle dst{std::floor((static_cast<float>(w) - static_cast<float>(iw) * scale) * 0.5f),
                        std::floor((static_cast<float>(h) - static_cast<float>(ih) * scale) * 0.5f),
                        static_cast<float>(iw) * scale, static_cast<float>(ih) * scale};
    ps1Rect_ = dst;

    BeginTextureMode(viewTarget_->Get());
    ClearBackground(BLACK);
    engine_.Post().Draw(sceneTarget_->Get().texture, dst);
    EndTextureMode();
  } else {
    ps1Rect_ = {0.0f, 0.0f, static_cast<float>(w), static_cast<float>(h)};

    BeginTextureMode(viewTarget_->Get());
    ClearBackground(clear);
    BeginMode3D(cam);
    DrawWorld(cam);
    EndMode3D();
    EndTextureMode();
  }
}

void EditorApp::DrawWorld(const Camera3D& camera) {
  const retro::RetroShaderParams& p = engine_.Retro().Params();

  lights_.clear();
  if (p.pointLights) {
    scene_->CollectLights(lights_);
    retro::KeepNearestLights(lights_, camera.position,
                             static_cast<std::size_t>(retro::kMaxPointLights));
  }
  engine_.Retro().SetPointLights(lights_);

  scene_->Draw(camera, engine_.Retro().Sprite()->Get(), p.ambient, lights_);
  DrawGizmos();
  DrawSelectionGizmo();
}

void EditorApp::DrawGizmos() {
  if (showGrid_)
    DrawGrid(40, 1.0f);

  if (showColliders_) {
    std::vector<retro::WorldBox> boxes;
    scene_->CollectColliders(boxes);
    for (const retro::WorldBox& b : boxes)
      retro::DrawWorldBoxWires(b, {255, 80, 80, 255});
  }

  if (showTriggers_) {
    for (const retro::Trigger& t : scene_->triggers) {
      retro::DrawWorldBoxWires(t.box,
                               t.marker ? Color{180, 130, 255, 255} : Color{80, 255, 120, 255});
    }
  }

  if (showLights_) {
    std::vector<retro::PointLight> all;
    scene_->CollectLights(all);
    for (const retro::PointLight& l : all) {
      const Color c = ToColor(l.color);
      DrawSphereWires(l.position, 0.18f, 6, 6, c);
      DrawCircle3D(l.position, l.radius, {1.0f, 0.0f, 0.0f}, 90.0f, {c.r, c.g, c.b, 90});
    }
  }

  if (showEmitters_) {
    std::vector<retro::Emitter> all;
    scene_->CollectEmitters(all);
    for (const retro::Emitter& e : all) {
      DrawSphereWires(e.position, 0.18f, 6, 6, {90, 170, 255, 255});
      DrawCircle3D(e.position, e.radius, {1.0f, 0.0f, 0.0f}, 90.0f, {90, 170, 255, 90});
    }
  }

  if (showBounds_ && scene_->bounds) {
    const retro::Scene::Bounds& b = *scene_->bounds;
    const Color c{255, 170, 60, 255};
    const float y = 0.05f;
    DrawLine3D({b.min.x, y, b.min.y}, {b.max.x, y, b.min.y}, c);
    DrawLine3D({b.max.x, y, b.min.y}, {b.max.x, y, b.max.y}, c);
    DrawLine3D({b.max.x, y, b.max.y}, {b.min.x, y, b.max.y}, c);
    DrawLine3D({b.min.x, y, b.max.y}, {b.min.x, y, b.min.y}, c);
  }

  if (showSpawn_) {
    const Vector3 s = scene_->spawn.position;
    DrawCylinderWires(s, 0.3f, 0.3f, 1.7f, 8, {80, 220, 255, 255});
    const float yaw = scene_->spawn.yaw * DEG2RAD;
    DrawLine3D({s.x, s.y + 1.5f, s.z},
               {s.x - std::sin(yaw) * 1.2f, s.y + 1.5f, s.z - std::cos(yaw) * 1.2f},
               {80, 220, 255, 255});
  }
}

void EditorApp::DrawSelectionGizmo() {
  const Json* item = SelectedJson();
  if (!item)
    return;
  const Color yellow{255, 230, 60, 255};

  switch (selection_.kind) {
  case SelectionKind::Entity: {
    if (static_cast<std::size_t>(selection_.index) >= scene_->Count())
      break;
    retro::Entity& e = scene_->EntityAt(static_cast<std::size_t>(selection_.index));
    DrawOrientedBox(EntityMatrix(e), e.LocalBounds(), yellow);
    if (e.collider) {
      retro::DrawWorldBoxWires(retro::MakeWorldBox(*e.collider, e.transform), {255, 140, 60, 255});
    }
    break;
  }
  case SelectionKind::Trigger:
    DrawOrientedBox(TriggerMatrix(*item), TriggerLocalBox(*item), yellow);
    break;
  case SelectionKind::Light:
  case SelectionKind::Emitter: {
    const Vector3 p = ReadVec3(item, "position", {0.0f, 0.0f, 0.0f});
    const float radius =
        ReadNumber(item, "radius", selection_.kind == SelectionKind::Light ? 8.0f : 10.0f);
    DrawSphereWires(p, 0.3f, 8, 8, yellow);
    DrawCircle3D(p, radius, {1.0f, 0.0f, 0.0f}, 90.0f, {yellow.r, yellow.g, yellow.b, 120});
    break;
  }
  case SelectionKind::None:
    break;
  }

  Vector3 p{};
  if (!GizmoPosition(p))
    return;
  const float len = GizmoLength(p);

  rlDrawRenderBatchActive();
  rlDisableDepthTest();
  for (int axis = 0; axis < 3; ++axis) {
    const Color color = (gizmoHover_ == axis || gizmoDrag_ == axis) ? yellow : AxisColor(axis);
    const Vector3 dir = AxisVector(axis);
    const Vector3 end = Vector3Add(p, Vector3Scale(dir, len));
    const Vector3 shaft = Vector3Add(p, Vector3Scale(dir, len * 0.82f));
    DrawCylinderEx(p, shaft, len * 0.012f, len * 0.012f, 6, color);
    DrawCylinderEx(shaft, end, len * 0.05f, 0.0f, 8, color);
  }
  DrawSphere(p, len * 0.07f,
             (gizmoHover_ == 3 || gizmoDrag_ == 3) ? yellow : Color{235, 235, 235, 255});
  rlDrawRenderBatchActive();
  rlEnableDepthTest();
}

// ---------------------------------------------------------------------------
// Raton: rayo, seleccion y gizmo
// ---------------------------------------------------------------------------
// 'mouse' va relativo a la esquina de la imagen del visor. En la vista PS1 la imagen tiene
// bandas negras: el rayo se calcula sobre el rectangulo 4:3.
bool EditorApp::MouseRay(Vector2 mouse, Ray& out) const {
  Vector2 local = mouse;
  float w = static_cast<float>(viewW_);
  float h = static_cast<float>(viewH_);
  if (ps1View_) {
    if (ps1Rect_.width < 1.0f || ps1Rect_.height < 1.0f)
      return false;
    local = {mouse.x - ps1Rect_.x, mouse.y - ps1Rect_.y};
    w = ps1Rect_.width;
    h = ps1Rect_.height;
  }
  if (w < 1.0f || h < 1.0f)
    return false;
  out = GetScreenToWorldRayEx(local, camera_.Raw(), static_cast<int>(w), static_cast<int>(h));
  return true;
}

// Todo lo que el rayo toca, ordenado: objetos, luego triggers, luego suelos planos; dentro de
// cada grupo, de mas cerca a mas lejos.
std::vector<EditorApp::Hit> EditorApp::CollectHits(const Ray& ray) const {
  std::vector<Hit> hits;
  if (!scene_ || !doc_.is_object())
    return hits;

  auto count = [&](const char* key) -> int {
    return (doc_.contains(key) && doc_.at(key).is_array()) ? static_cast<int>(doc_.at(key).size())
                                                           : 0;
  };

  const int entities = std::min(count("entities"), static_cast<int>(scene_->Count()));
  for (int i = 0; i < entities; ++i) {
    retro::Entity& e = scene_->EntityAt(static_cast<std::size_t>(i));
    const BoundingBox box = e.LocalBounds();
    float distance = 0.0f;
    if (!RayOrientedBox(ray, EntityMatrix(e), box, distance))
      continue;
    const float height = (box.max.y - box.min.y) * std::fabs(e.transform.scale.y);
    hits.push_back({SelectionKind::Entity, i, distance, height < 0.15f ? 2 : 0});
  }

  if (showTriggers_) {
    const int n = count("triggers");
    for (int i = 0; i < n; ++i) {
      const Json& t = doc_.at("triggers")[static_cast<std::size_t>(i)];
      float distance = 0.0f;
      if (RayOrientedBox(ray, TriggerMatrix(t), TriggerLocalBox(t), distance)) {
        hits.push_back({SelectionKind::Trigger, i, distance, 1});
      }
    }
  }

  auto pointLike = [&](const char* key, SelectionKind kind, bool visible) {
    if (!visible)
      return;
    const int n = count(key);
    for (int i = 0; i < n; ++i) {
      const Json& item = doc_.at(key)[static_cast<std::size_t>(i)];
      const RayCollision c =
          GetRayCollisionSphere(ray, ReadVec3(&item, "position", {0.0f, 0.0f, 0.0f}), kPickRadius);
      if (c.hit && std::isfinite(c.distance))
        hits.push_back({kind, i, c.distance, 0});
    }
  };
  pointLike("lights", SelectionKind::Light, showLights_);
  pointLike("emitters", SelectionKind::Emitter, showEmitters_);

  std::sort(hits.begin(), hits.end(), [](const Hit& a, const Hit& b) {
    return a.rank != b.rank ? a.rank < b.rank : a.distance < b.distance;
  });
  return hits;
}

void EditorApp::PickAt(Vector2 mouse) {
  if (ps1View_ && !CheckCollisionPointRec(mouse, ps1Rect_))
    return;
  Ray ray{};
  if (!MouseRay(mouse, ray))
    return;

  const std::vector<Hit> hits = CollectHits(ray);
  const bool sameSpot = Vector2Distance(mouse, lastPick_) < 4.0f;
  lastPick_ = mouse;

  if (hits.empty()) {
    selection_ = {};
    return;
  }

  std::size_t pick = 0;
  if (sameSpot) {
    for (std::size_t i = 0; i < hits.size(); ++i) {
      if (hits[i].kind == selection_.kind && hits[i].index == selection_.index) {
        pick = (i + 1) % hits.size();
        break;
      }
    }
  }
  selection_ = {hits[pick].kind, hits[pick].index};
}

bool EditorApp::GizmoPosition(Vector3& out) const {
  const Json* item = SelectedJson();
  if (!item)
    return false;
  out = ReadVec3(item, "position", {0.0f, 0.0f, 0.0f});
  return true;
}

float EditorApp::GizmoLength(Vector3 at) const {
  return std::max(0.4f, Vector3Distance(camera_.Position(), at) * 0.16f);
}

// 0, 1, 2 = ejes X, Y, Z; 3 = esfera central (mover sobre el suelo); -1 = nada.
int EditorApp::GizmoHitTest(const Ray& ray) const {
  Vector3 p{};
  if (!GizmoPosition(p))
    return -1;
  const float len = GizmoLength(p);

  const RayCollision center = GetRayCollisionSphere(ray, p, len * 0.12f);
  if (center.hit && std::isfinite(center.distance))
    return 3;

  int best = -1;
  float bestDist = len * 0.09f;
  for (int axis = 0; axis < 3; ++axis) {
    float s = 0.0f;
    const float d =
        RaySegmentDistance(ray, p, Vector3Add(p, Vector3Scale(AxisVector(axis), len)), s);
    if (d < bestDist) {
      bestDist = d;
      best = axis;
    }
  }
  return best;
}

void EditorApp::BeginGizmoDrag(int handle, const Ray& ray) {
  Vector3 p{};
  if (!GizmoPosition(p))
    return;

  if (handle <= 2) {
    float t = 0.0f;
    if (!AxisParameter(ray, p, AxisVector(handle), t))
      return;
    dragParam_ = t;
  } else {
    if (std::fabs(ray.direction.y) < 1e-4f)
      return;
    const float t = (p.y - ray.position.y) / ray.direction.y;
    if (t < 0.0f)
      return;
    dragGrab_ = Vector3Add(ray.position, Vector3Scale(ray.direction, t));
  }
  gizmoDrag_ = handle;
  dragOrigin_ = p;
}

void EditorApp::UpdateGizmoDrag(const Ray& ray) {
  Json* item = SelectedJsonMut();
  if (!item) {
    gizmoDrag_ = -1;
    return;
  }

  Vector3 next = dragOrigin_;
  if (gizmoDrag_ <= 2) {
    const Vector3 axis = AxisVector(gizmoDrag_);
    float t = 0.0f;
    if (!AxisParameter(ray, dragOrigin_, axis, t))
      return;
    next = Vector3Add(dragOrigin_, Vector3Scale(axis, t - dragParam_));
  } else {
    if (std::fabs(ray.direction.y) < 1e-4f)
      return;
    const float t = (dragOrigin_.y - ray.position.y) / ray.direction.y;
    if (t < 0.0f)
      return;
    const Vector3 hit = Vector3Add(ray.position, Vector3Scale(ray.direction, t));
    next = Vector3Add(dragOrigin_, Vector3Subtract(hit, dragGrab_));
    next.y = dragOrigin_.y;
  }

  if (ImGui::GetIO().KeyCtrl)
    next = Snap(next);

  const Vector3 current = ReadVec3(item, "position", {0.0f, 0.0f, 0.0f});
  if (SameVec3(next, current))
    return;

  const ImGuiID id = ImGui::GetID("gizmo_arrastre");
  BeginChange(id);
  Store(*item, nullptr, "position", Vec3Json(next), SameVec3(next, {0.0f, 0.0f, 0.0f}));
  MarkChanged(id);
}

void EditorApp::HandleViewportMouse(Vector2 mouse) {
  Ray ray{};
  const bool haveRay = MouseRay(mouse, ray);

  if (gizmoDrag_ >= 0) {
    if (haveRay && ImGui::IsMouseDown(ImGuiMouseButton_Left)) {
      UpdateGizmoDrag(ray);
    } else {
      gizmoDrag_ = -1;
      gestureEnded_ = true;
    }
    return;
  }

  gizmoHover_ = -1;
  if (!viewportHovered_ || lookingNow_ || !haveRay)
    return;

  gizmoHover_ = GizmoHitTest(ray);
  if (gizmoHover_ >= 0)
    ImGui::SetMouseCursor(ImGuiMouseCursor_Hand);

  if (!ImGui::IsMouseClicked(ImGuiMouseButton_Left))
    return;
  if (gizmoHover_ >= 0)
    BeginGizmoDrag(gizmoHover_, ray);
  else
    PickAt(mouse);
}

} // namespace editor
