#include "world/SceneManager.hpp"

#include "core/RetroEngine.hpp"
#include "graphics/RetroCamera.hpp"
#include "world/SceneLoader.hpp"

#include <algorithm>
#include <cmath>
#include <iostream>
#include <stdexcept>
#include <utility>

namespace retro {

namespace {

constexpr float kFadeSteps = 8.0f;
constexpr float kMaxDt = 0.05f;

} // namespace

SceneManager::SceneManager(RetroEngine& engine, RetroCamera& camera,
                           std::vector<std::string> scenePaths, float fadeSeconds)
    : engine_(engine), camera_(camera), paths_(std::move(scenePaths)),
      fadeSeconds_(std::max(fadeSeconds, 0.01f)) {
  if (paths_.empty())
    throw std::invalid_argument("SceneManager: lista de escenas vacia");

  const RetroShaderParams& p = engine_.Retro().Params();
  defaults_ = {engine_.Config().clearColor, p.fogStart, p.fogEnd, p.lightDirection, p.ambient};
}

void SceneManager::ApplyEnvironment(const Environment& env) {
  engine_.SetClearColor(env.background); // actualiza tambien fogColor

  RetroShaderParams p = engine_.Retro().Params();
  p.fogStart = env.fogStart;
  p.fogEnd = env.fogEnd;
  p.lightDirection = env.lightDirection;
  p.ambient = env.ambient;
  engine_.Retro().SetParams(p);
}

void SceneManager::LoadNow(std::size_t index) {
  if (index >= paths_.size())
    throw std::out_of_range("SceneManager: indice de escena invalido");

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
  } catch (...) {
    camera_ = savedCamera;
    engine_.SetClearColor(savedBackground);
    engine_.Retro().SetParams(savedParams);
    throw;
  }

  // Libera modelos y texturas que solo usaba la escena anterior.
  engine_.Assets().PurgeUnused();
}

void SceneManager::GoTo(std::size_t index) {
  if (index >= paths_.size())
    throw std::out_of_range("SceneManager: indice de escena invalido");
  if (phase_ != Phase::Idle)
    return;

  pending_ = index;
  phase_ = Phase::FadingOut;
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
      phase_ = Phase::FadingIn;
    }
    break;

  case Phase::FadingIn:
    fade_ = std::max(fade_ - dt / fadeSeconds_, 0.0f);
    if (fade_ <= 0.0f)
      phase_ = Phase::Idle;
    break;
  }

  if (scene_)
    scene_->Update(dt);
}

void SceneManager::Draw() {
  if (scene_)
    scene_->Draw();
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