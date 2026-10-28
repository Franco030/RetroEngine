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
#include <iostream>
#include <unordered_set>
#include <stdexcept>
#include <utility>

namespace retro {

namespace {

constexpr float kFadeSteps = 8.0f;
constexpr double kPrefetchBudgetMs = 3.0;      // por fotograma jugando (a 60 fps hay 16.6 ms)
constexpr double kPrefetchDarkBudgetMs = 40.0; // mientras la pantalla sigue casi negra al llegar

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

// Lee un JSON de escena; vacio si no se puede (la precarga es una ayuda: nunca falla).
nlohmann::json ReadSceneJson(const std::string& fullPath) {
  std::ifstream file(fullPath);
  if (!file)
    return {};
  return nlohmann::json::parse(file, nullptr, false); // sin excepciones: discarded si esta mal
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
               p.sun};
}

void SceneManager::ApplyEnvironment(const Environment& env) {
  engine_.SetClearColor(env.background); // actualiza tambien fogColor

  RetroShaderParams p = engine_.Retro().Params();
  p.fogStart = env.fogStart;
  p.fogEnd = env.fogEnd;
  p.lightDirection = env.lightDirection;
  p.ambient = env.ambient;
  p.sun = env.sun;
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

  const auto start = std::chrono::steady_clock::now();
  engine_.Assets().ResetStats();

  // Copias para deshacer si el JSON falla a medias.
  const RetroCamera savedCamera = camera_;
  const RetroShaderParams savedParams = engine_.Retro().Params();
  const Color savedBackground = engine_.Config().clearColor;

  try {
    ApplyEnvironment(defaults_);
    camera_ = RetroCamera{};

    // La nueva escena se construye aparte: la actual sigue viva hasta el final.
    auto next = std::make_unique<Scene>();
    LoadScene(paths_[index], engine_, *next, camera_);

    scene_ = std::move(next); // la escena anterior se destruye aqui
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

  // Retiene lo de las escenas vecinas que ya estaba cargado (por ejemplo, la escena de la que se
  // viene) y libera lo demas que solo usaba la escena anterior.
  PlanPrefetch();
  engine_.Assets().PurgeUnused();

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
  pending_ = index;
  phase_ = Phase::FadingOut;
}

void SceneManager::GoToPath(const std::string& relativePath, std::optional<Vector3> spawnPosition,
                            std::optional<float> spawnYaw) {
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
  phase_ = Phase::Idle;
  fade_ = 0.0f;

  scene_.reset();
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

void SceneManager::PlanPrefetch() {
  AssetManager& assets = engine_.Assets();
  const nlohmann::json actual = ReadSceneJson(assets.Root() + paths_[index_]);
  std::vector<std::string> vecinas;
  if (!actual.is_discarded())
    CollectGotos(actual, vecinas);
  std::sort(vecinas.begin(), vecinas.end());
  vecinas.erase(std::unique(vecinas.begin(), vecinas.end()), vecinas.end());
  std::erase(vecinas, paths_[index_]);

  std::vector<std::shared_ptr<ModelResource>> models;
  std::vector<std::shared_ptr<TextureResource>> textures;
  std::unordered_set<std::string> vistos; // cada recurso una vez (muchos arboles comparten modelo)
  prefetch_.clear();
  int enMemoria = 0;
  for (const std::string& v : vecinas) {
    const nlohmann::json escena = ReadSceneJson(assets.Root() + v);
    if (escena.is_discarded() || !escena.contains("entities"))
      continue;
    for (const nlohmann::json& e : escena.at("entities")) {
      auto pedir = [&](const char* key, bool model) {
        const auto it = e.find(key);
        if (it == e.end() || !it->is_string())
          return;
        const std::string path = it->get<std::string>();
        if (!vistos.insert((model ? "m:" : "t:") + path).second)
          return;
        if (model ? assets.HasModel(path) : assets.HasTexture(path)) {
          ++enMemoria; // ya esta: basta con retenerlo
          if (model)
            models.push_back(assets.GetModel(path));
          else
            textures.push_back(assets.GetTexture(path));
        } else {
          prefetch_.push_back({model, path});
        }
      };
      pedir("model", true);
      pedir("texture", false);
      pedir("sprite", false);
    }
  }
  // lo mas pesado primero: se carga mientras la pantalla sigue negra (ver StepPrefetch)
  auto peso = [&](const PrefetchItem& item) {
    std::error_code ec;
    const auto bytes = std::filesystem::file_size(assets.Root() + item.path, ec);
    return ec ? std::uintmax_t{0} : bytes;
  };
  std::stable_sort(prefetch_.begin(), prefetch_.end(),
                   [&](const PrefetchItem& a, const PrefetchItem& b) { return peso(a) > peso(b); });

  keepModels_ = std::move(models); // lo retenido antes y no repetido se suelta aqui
  keepTextures_ = std::move(textures);
  prefetchMs_ = prefetchMaxMs_ = 0.0;
  prefetchFrames_ = prefetchCount_ = 0;

  std::cout << "[precarga] vecinas:";
  for (const std::string& v : vecinas)
    std::cout << ' ' << v;
  std::cout << " | ya en memoria " << enMemoria << " | por cargar " << prefetch_.size() << '\n';
}

void SceneManager::StepPrefetch() {
  if (prefetch_.empty())
    return;
  AssetManager& assets = engine_.Assets();
  // Con la pantalla aun casi negra (llegando a la escena) el tiron no se ve: se aprovecha para
  // lo pesado. Jugando, solo un poco por fotograma.
  const double budget = fade_ > 0.5f ? kPrefetchDarkBudgetMs : kPrefetchBudgetMs;
  const auto start = Clock::now();
  while (!prefetch_.empty() && MsSince(start) < budget) {
    const PrefetchItem item = std::move(prefetch_.front());
    prefetch_.pop_front();
    try {
      if (item.model)
        keepModels_.push_back(assets.GetModel(item.path));
      else
        keepTextures_.push_back(assets.GetTexture(item.path));
      ++prefetchCount_;
    } catch (const std::exception& e) {
      std::cerr << "[precarga] no se pudo cargar " << item.path << ": " << e.what() << '\n';
    }
  }
  const double ms = MsSince(start);
  prefetchMs_ += ms;
  if (budget == kPrefetchBudgetMs) // el tiron que se nota: jugando, no con la pantalla negra
    prefetchMaxMs_ = std::max(prefetchMaxMs_, ms);
  ++prefetchFrames_;
  if (prefetch_.empty()) {
    std::cout << "[precarga] terminada: " << prefetchCount_ << " recursos en "
              << static_cast<int>(prefetchMs_) << " ms repartidos en " << prefetchFrames_
              << " fotogramas (el peor jugando: " << static_cast<int>(prefetchMaxMs_)
              << " ms)\n";
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

void SceneManager::DrawFade() const {
  if (fade_ <= 0.0f)
    return;

  const float stepped = std::floor(fade_ * kFadeSteps) / kFadeSteps;
  const auto alpha = static_cast<unsigned char>(stepped * 255.0f + 0.5f);
  DrawRectangle(0, 0, engine_.Config().internalWidth, engine_.Config().internalHeight,
                Color{0, 0, 0, alpha});
}

} // namespace retro