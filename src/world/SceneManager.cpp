#include "world/SceneManager.hpp"
#include "core/RetroEngine.hpp"
#include "graphics/RetroCamera.hpp"
#include "world/SceneLoader.hpp"

#include <raymath.h>

#include <nlohmann/json.hpp>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <functional>
#include <iostream>
#include <unordered_set>
#include <stdexcept>
#include <utility>

namespace retro {

namespace {

constexpr float kFadeSteps = 8.0f;
// Subir a la GPU lo que los hilos ya leyeron (la lectura no cuesta al hilo principal).
constexpr double kPrefetchBudgetMs = 3.0;      // por fotograma jugando (a 60 fps hay 16.6 ms)
constexpr double kPrefetchDarkBudgetMs = 12.0; // mientras la pantalla sigue casi negra al llegar

using Clock = std::chrono::steady_clock;

double MsSince(Clock::time_point start) {
  return std::chrono::duration<double, std::milli>(Clock::now() - start).count();
}

// Escenas a las que se puede ir desde 'j': cualquier "goto_scene" (triggers, interacciones).
void CollectGotos(const nlohmann::json& j, std::vector<std::string>& out) {
  if (j.is_object()) {
    if (const auto it = j.find("goto_scene"); it != j.end() && it->is_string())
      out.push_back(it->get<std::string>());
    for (const auto& v : j)
      CollectGotos(v, out);
  } else if (j.is_array()) {
    for (const auto& v : j)
      CollectGotos(v, out);
  }
}

// Modelos y texturas que usa una escena (entidades y panorama), sin repetir.
void CollectAssets(const nlohmann::json& scene, std::vector<AssetRef>& out,
                   std::unordered_set<std::string>& seen) {
  using K = AssetRef::Kind;
  auto push = [&](K kind, const std::string& path) {
    if (seen.insert(std::to_string(static_cast<int>(kind)) + ":" + path).second)
      out.push_back({kind, path});
  };
  auto add = [&](K kind, const nlohmann::json& j, const char* key) {
    if (const auto it = j.find(key); it != j.end() && it->is_string())
      push(kind, it->get<std::string>());
  };
  if (const auto e = scene.find("entities"); e != scene.end() && e->is_array()) {
    for (const nlohmann::json& ent : *e) {
      add(K::Model, ent, "model");
      add(K::Texture, ent, "texture");
      add(K::Texture, ent, "sprite");
    }
  }
  if (const auto p = scene.find("panorama"); p != scene.end() && p->is_object()) {
    add(K::Model, *p, "model");
    if (const auto sun = p->find("sun"); sun != p->end() && sun->is_object())
      add(K::Texture, *sun, "texture");
  }
  // sonidos cortos: los pasos y los "play_sound" de la logica (la musica y el ambiente son
  // streams: se abren al llegar, en el hilo de audio no hay nada que precargar)
  if (const auto a = scene.find("audio"); a != scene.end() && a->is_object()) {
    if (const auto f = a->find("footsteps"); f != a->end() && f->is_object() && f->contains("files")) {
      for (const nlohmann::json& file : f->at("files")) {
        if (file.is_string())
          push(K::Sound, file.get<std::string>());
      }
    }
  }
  std::function<void(const nlohmann::json&)> sonidos = [&](const nlohmann::json& j) {
    if (j.is_object()) {
      add(K::Sound, j, "play_sound");
      for (const auto& v : j)
        sonidos(v);
    } else if (j.is_array()) {
      for (const auto& v : j)
        sonidos(v);
    }
  };
  for (const char* key : {"entities", "triggers"}) {
    if (const auto it = scene.find(key); it != scene.end())
      sonidos(*it);
  }
}

void Request(AssetManager& assets, const AssetRef& item) {
  switch (item.kind) {
  case AssetRef::Kind::Model: assets.RequestModel(item.path); break;
  case AssetRef::Kind::Texture: assets.RequestTexture(item.path); break;
  case AssetRef::Kind::Sound: assets.RequestSound(item.path); break;
  }
}

bool Has(const AssetManager& assets, const AssetRef& item) {
  switch (item.kind) {
  case AssetRef::Kind::Model: return assets.HasModel(item.path);
  case AssetRef::Kind::Texture: return assets.HasTexture(item.path);
  case AssetRef::Kind::Sound: return assets.HasSound(item.path);
  }
  return false;
}

// Guarda un shared_ptr al recurso (ya cargado) para que la purga no lo suelte.
void Keep(AssetManager& assets, const AssetRef& item,
          std::vector<std::shared_ptr<ModelResource>>& models,
          std::vector<std::shared_ptr<TextureResource>>& textures,
          std::vector<std::shared_ptr<SoundResource>>& sounds) {
  switch (item.kind) {
  case AssetRef::Kind::Model: models.push_back(assets.GetModel(item.path)); break;
  case AssetRef::Kind::Texture: textures.push_back(assets.GetTexture(item.path)); break;
  case AssetRef::Kind::Sound: sounds.push_back(assets.GetSound(item.path)); break;
  }
}

std::filesystem::file_time_type Stamp(const std::string& fullPath) {
  std::error_code ec;
  return std::filesystem::last_write_time(fullPath, ec);
}
constexpr float kMaxDt = 0.05f;

} // namespace

SceneManager::SceneManager(RetroEngine& engine, RetroCamera& camera,
                           std::vector<std::string> scenePaths, float fadeSeconds)
    : engine_(engine), camera_(camera), paths_(std::move(scenePaths)),
      fadeSeconds_(std::max(fadeSeconds, 0.01f)) {
  if (paths_.empty())
    throw std::invalid_argument("SceneManager: lista de escenas vacia");

  const RetroShaderParams& p = engine_.Retro().Params();
  defaults_ = {engine_.Config().clearColor, p.fogStart, p.fogEnd, p.lightDirection, p.ambient,
               p.sun, p.mistDensity, p.mistHeight, p.mistColor};
}

void SceneManager::ApplyEnvironment(const Environment& env) {
  engine_.SetClearColor(env.background); // actualiza tambien fogColor

  RetroShaderParams p = engine_.Retro().Params();
  p.fogStart = env.fogStart;
  p.fogEnd = env.fogEnd;
  p.lightDirection = env.lightDirection;
  p.ambient = env.ambient;
  p.sun = env.sun;
  p.mistDensity = env.mistDensity;
  p.mistHeight = env.mistHeight;
  p.mistColor = env.mistColor;
  engine_.Retro().SetParams(p);
}

void SceneManager::ApplyAudio(const SceneAudio& a) {
  AudioSystem& audio = engine_.Audio();
  audio.ClearEmitters();
  audio.PlayMusic(a.music, a.musicVolume, a.fade);
  audio.PlayAmbient(a.ambient, a.ambientVolume, a.fade);
  audio.SetFootsteps(a.footsteps, a.footstepVolume);
}

void SceneManager::UpdateAudio(float dt) {
  AudioSystem& audio = engine_.Audio();

  const Vector3 pos = camera_.Position();
  audio.SetListener(pos, Vector3Subtract(camera_.Target(), pos));

  emitters_.clear();
  if (scene_)
    scene_->CollectEmitters(emitters_);
  audio.UpdateEmitters(emitters_, dt);
}

void SceneManager::LoadNow(std::size_t index) {
  if (index >= paths_.size())
    throw std::out_of_range("SceneManager: indice de escena invalido");

  const auto start = Clock::now();
  double tLoad = 0.0, tSwap = 0.0; // marcas para el informe
  std::shared_ptr<const nlohmann::json> current;
  engine_.Assets().ResetStats();

  // Copias para deshacer si el JSON falla a medias.
  const RetroCamera savedCamera = camera_;
  const RetroShaderParams savedParams = engine_.Retro().Params();
  const Color savedBackground = engine_.Config().clearColor;

  try {
    ApplyEnvironment(defaults_);
    camera_ = RetroCamera{};

    // La nueva escena se construye aparte: la actual sigue viva hasta el final.
    // El JSON ya parseado (si la precarga lo tenia) y todos sus modelos y texturas pedidos a
    // los hilos de una vez: se leen en paralelo mientras se construyen las entidades, y cada
    // GetModel solo espera al suyo.
    const std::shared_ptr<const nlohmann::json> doc = SceneJson(paths_[index]);
    std::vector<AssetRef> own;
    std::unordered_set<std::string> seen;
    CollectAssets(*doc, own, seen);
    AssetManager& assets = engine_.Assets();
    for (const AssetRef& item : own)
      Request(assets, item);
    current = doc;

    auto next = std::make_unique<Scene>();
    LoadSceneFromJson(*doc, paths_[index], engine_, *next, camera_);
    tLoad = MsSince(start);

    scene_ = std::move(next); // la escena anterior se destruye aqui
    tSwap = MsSince(start);
    index_ = index;
    ++generation_;

    if (pendingSpawnPos_)
      scene_->spawn.position = *pendingSpawnPos_;
    if (pendingSpawnYaw_)
      scene_->spawn.yaw = *pendingSpawnYaw_;
  } catch (...) {
    camera_ = savedCamera;
    engine_.SetClearColor(savedBackground);
    engine_.Retro().SetParams(savedParams);
    throw;
  }

  ApplyAudio(scene_->audio);
  const double tAudio = MsSince(start);

  // Las vecinas se leen en un hilo; la purga de lo que solo usaba la escena anterior espera a
  // saber que necesitan (asi no se suelta lo que habria que volver a cargar).
  PlanPrefetch(current);
  std::cout << "[carga] fases: escena " << static_cast<int>(tLoad) << " | soltar la anterior "
            << static_cast<int>(tSwap - tLoad) << " | audio " << static_cast<int>(tAudio - tSwap)
            << " | planear precarga " << static_cast<int>(MsSince(start) - tAudio) << " ms\n";

  // Cuanto ha costado: es el tiron que queda tapado por el fundido en negro.
  const double total =
      std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start).count();
  const AssetManager::LoadStats& st = engine_.Assets().Stats();
  std::cout << "[carga] " << paths_[index] << ": " << static_cast<int>(total) << " ms | modelos "
            << st.models << " (" << static_cast<int>(st.modelMs) << " ms) | texturas "
            << st.textures << " (" << static_cast<int>(st.textureMs) << " ms) | sonidos "
            << st.sounds << " (" << static_cast<int>(st.soundMs) << " ms) | resto "
            << static_cast<int>(total - st.modelMs - st.textureMs - st.soundMs) << " ms\n";
}

void SceneManager::FadeInFromBlack() {
  phase_ = Phase::FadingIn;
  fade_ = 1.0f;
}

void SceneManager::GoTo(std::size_t index) {
  if (index >= paths_.size())
    throw std::out_of_range("SceneManager: indice de escena invalido");
  if (phase_ != Phase::Idle)
    return;

  pendingSpawnPos_.reset();
  pendingSpawnYaw_.reset();
  pendingInterlude_ = {};
  pending_ = index;
  phase_ = Phase::FadingOut;
}

void SceneManager::GoToPath(const std::string& relativePath, std::optional<Vector3> spawnPosition,
                            std::optional<float> spawnYaw, Interlude interlude) {
  if (phase_ != Phase::Idle)
    return;

  auto it = std::find(paths_.begin(), paths_.end(), relativePath);
  if (it == paths_.end()) {
    if (!FileExists((engine_.Assets().Root() + relativePath).c_str())) {
      throw std::invalid_argument("escena de destino inexistente: " + relativePath);
    }
    paths_.push_back(relativePath);
    it = paths_.end() - 1;
  }

  GoTo(static_cast<std::size_t>(it - paths_.begin()));
  pendingSpawnPos_ = spawnPosition;
  pendingSpawnYaw_ = spawnYaw;
  pendingInterlude_ = std::move(interlude);
}

void SceneManager::Next() {
  if (paths_.size() > 1)
    GoTo((index_ + 1) % paths_.size());
}

void SceneManager::Previous() {
  if (paths_.size() > 1)
    GoTo((index_ + paths_.size() - 1) % paths_.size());
}

void SceneManager::Reload() {
  pendingSpawnPos_.reset();
  pendingSpawnYaw_.reset();
  pendingInterlude_ = {};
  interlude_ = {};
  phase_ = Phase::Idle;
  fade_ = 0.0f;

  scene_.reset();
  docs_.clear(); // F5: todo se vuelve a leer del disco
  engine_.Assets().PurgeUnused();
  LoadNow(index_);
}

void SceneManager::Update(float dt) {
  dt = std::min(dt, kMaxDt);

  switch (phase_) {
  case Phase::Idle:
    break;

  case Phase::FadingOut:
    fade_ = std::min(fade_ + dt / fadeSeconds_, 1.0f);
    if (fade_ >= 1.0f) {
      // Pantalla en negro: la carga (y su tirón) no se ve.
      try {
        LoadNow(pending_);
      } catch (const std::exception& e) {
        std::cerr << "No se pudo cambiar de escena: " << e.what() << '\n';
      }
      pendingSpawnPos_.reset();
      pendingSpawnYaw_.reset();

      interlude_ = std::exchange(pendingInterlude_, {});
      darkTime_ = 0.0f;
      phase_ = interlude_.seconds > 0.0f ? Phase::Dark : Phase::FadingIn;
    }
    break;

  case Phase::Dark:
    darkTime_ += dt;
    if (darkTime_ >= interlude_.seconds) {
      interlude_ = {};
      phase_ = Phase::FadingIn;
    }
    break;

  case Phase::FadingIn:
    fade_ = std::max(fade_ - dt / fadeSeconds_, 0.0f);
    if (fade_ <= 0.0f)
      phase_ = Phase::Idle;
    break;
  }

  if (phase_ != Phase::FadingOut) // jugando o llegando (al irse, la carga ya va a ocurrir)
    StepPrefetch();

  if (scene_)
    scene_->Update(dt, focus_);
}

std::shared_ptr<const nlohmann::json> SceneManager::SceneJson(const std::string& path) {
  const std::string full = engine_.Assets().Root() + path;
  if (auto it = docs_.find(path); it != docs_.end() && it->second.json &&
                                  it->second.stamp == Stamp(full))
    return it->second.json;
  std::ifstream file(full);
  if (!file)
    throw std::runtime_error("No se pudo abrir la escena: " + full);
  try {
    return std::make_shared<const nlohmann::json>(nlohmann::json::parse(file));
  } catch (const std::exception& e) {
    throw std::runtime_error(path + " (parseando JSON): " + e.what());
  }
}

void SceneManager::PlanPrefetch(std::shared_ptr<const nlohmann::json> current) {
  const std::string root = engine_.Assets().Root();
  const std::string self = paths_[index_];
  purgePending_ = true;
  // En un hilo: buscar las vecinas, leer y parsear sus JSON y listar lo que usan. Solo lee
  // archivos; el resultado lo recoge StepPrefetch.
  plan_ = engine_.Assets().Loader().Submit([root, self, current] {
    PrefetchPlan plan;
    std::vector<std::string> vecinas;
    if (current)
      CollectGotos(*current, vecinas);
    std::sort(vecinas.begin(), vecinas.end());
    vecinas.erase(std::unique(vecinas.begin(), vecinas.end()), vecinas.end());
    std::erase(vecinas, self);
    std::unordered_set<std::string> seen;
    for (const std::string& v : vecinas) {
      const std::string full = root + v;
      std::ifstream file(full);
      if (!file)
        continue;
      auto j = nlohmann::json::parse(file, nullptr, false); // la precarga nunca falla
      if (j.is_discarded())
        continue;
      auto doc = std::make_shared<const nlohmann::json>(std::move(j));
      CollectAssets(*doc, plan.items, seen);
      plan.docs.emplace(v, SceneDoc{doc, Stamp(full)});
      plan.neighbors.push_back(v);
    }
    return plan;
  });
}

void SceneManager::StepPrefetch() {
  AssetManager& assets = engine_.Assets();
  const auto start = Clock::now();

  // 1) El plan de un hilo esta listo: retener lo que ya hay, pedir lo que falta y purgar.
  if (plan_.valid() && plan_.wait_for(std::chrono::seconds(0)) == std::future_status::ready) {
    PrefetchPlan plan;
    try {
      plan = plan_.get();
    } catch (const std::exception& e) {
      std::cerr << "[precarga] " << e.what() << '\n';
    }
    docs_ = std::move(plan.docs);
    std::vector<std::shared_ptr<ModelResource>> models;
    std::vector<std::shared_ptr<TextureResource>> textures;
    prefetch_.clear();
    int enMemoria = 0;
    std::vector<std::shared_ptr<SoundResource>> sounds;
    for (const AssetRef& item : plan.items) {
      if (Has(assets, item)) {
        ++enMemoria;
        Keep(assets, item, models, textures, sounds);
      } else {
        Request(assets, item);
        prefetch_.push_back(item);
      }
    }
    keepModels_ = std::move(models); // lo retenido antes y no repetido se suelta aqui
    keepTextures_ = std::move(textures);
    keepSounds_ = std::move(sounds);
    if (purgePending_) {
      assets.PurgeUnused();
      purgePending_ = false;
    }
    prefetchMs_ = prefetchMaxMs_ = 0.0;
    prefetchFrames_ = prefetchCount_ = 0;
    std::cout << "[precarga] vecinas:";
    for (const std::string& v : plan.neighbors)
      std::cout << ' ' << v;
    std::cout << " | ya en memoria " << enMemoria << " | por cargar " << prefetch_.size() << '\n';
  }

  if (prefetch_.empty())
    return;

  // 2) Subir a la GPU lo que los hilos ya leyeron, con presupuesto: con la pantalla aun casi
  // negra (llegando) se puede gastar mas.
  const double budget = fade_ > 0.5f ? kPrefetchDarkBudgetMs : kPrefetchBudgetMs;
  assets.PumpUploads(budget);

  // 3) Retener lo que ya esta subido; lo que fallo (ni cargado ni pendiente) se olvida.
  std::erase_if(prefetch_, [&](const AssetRef& item) {
    if (Has(assets, item)) {
      Keep(assets, item, keepModels_, keepTextures_, keepSounds_);
      ++prefetchCount_;
      return true;
    }
    return !assets.IsPending(item.path);
  });

  const double ms = MsSince(start);
  prefetchMs_ += ms;
  prefetchMaxMs_ = std::max(prefetchMaxMs_, ms);
  ++prefetchFrames_;
  if (prefetch_.empty()) {
    std::cout << "[precarga] terminada: " << prefetchCount_ << " recursos, "
              << static_cast<int>(prefetchMs_) << " ms del hilo principal en " << prefetchFrames_
              << " fotogramas (el peor: " << static_cast<int>(prefetchMaxMs_) << " ms)\n";
  }
}

void SceneManager::Draw() {
  if (!scene_)
    return;

  const RetroShaderParams& p = engine_.Retro().Params();

  lights_.clear();
  if (p.pointLights) {
    scene_->CollectLights(lights_);
    KeepNearestLights(lights_, camera_.Position(), kMaxPointLights);
  }
  engine_.Retro().SetPointLights(lights_);

  scene_->Draw(camera_.Raw(), engine_.Retro().Sprite()->Get(), p.ambient, p.sun, lights_);
}

std::optional<SceneManager::Card> SceneManager::CurrentCard() const {
  if (phase_ != Phase::Dark || interlude_.lines.empty())
    return std::nullopt;
  // Una frase vacia es un rato de silencio, mas corto que el tiempo de leer una de verdad.
  constexpr float kSilence = 0.35f;
  auto weight = [](const std::string& s) { return s.empty() ? kSilence : 1.0f; };
  float total = 0.0f;
  for (const std::string& line : interlude_.lines)
    total += weight(line);
  const float unit = interlude_.seconds / total;
  float start = 0.0f;
  for (const std::string& text : interlude_.lines) {
    const float slot = weight(text) * unit;
    if (darkTime_ < start + slot || &text == &interlude_.lines.back()) {
      if (text.empty())
        return std::nullopt;
      const float t = darkTime_ - start;
      const float edge = std::min(0.9f, slot * 0.3f); // aparece y se apaga despacio
      return Card{text, std::clamp(std::min(t, slot - t) / edge, 0.0f, 1.0f)};
    }
    start += slot;
  }
  return std::nullopt;
}

void SceneManager::DrawFade() const {
  if (fade_ <= 0.0f)
    return;

  const float stepped = std::floor(fade_ * kFadeSteps) / kFadeSteps;
  const auto alpha = static_cast<unsigned char>(stepped * 255.0f + 0.5f);
  DrawRectangle(0, 0, engine_.Config().internalWidth, engine_.Config().internalHeight,
                Color{0, 0, 0, alpha});
}

} // namespace retro