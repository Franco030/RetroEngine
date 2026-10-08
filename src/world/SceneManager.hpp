#pragma once

#include "world/Scene.hpp"

#include <raylib.h>

#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace retro {

class RetroEngine;
class RetroCamera;

class SceneManager {
public:
  SceneManager(RetroEngine& engine, RetroCamera& camera, std::vector<std::string> scenePaths,
               float fadeSeconds = 0.35f);

  SceneManager(const SceneManager&) = delete;
  SceneManager& operator=(const SceneManager&) = delete;

  void LoadNow(std::size_t index);

  void GoTo(std::size_t index);
  void Next();
  void Previous();

  void Reload();

  void Update(float dt);
  void Draw();
  void DrawFade() const;

  std::size_t Index() const noexcept { return index_; }
  std::size_t Count() const noexcept { return paths_.size(); }
  const std::string& CurrentPath() const noexcept { return paths_[index_]; }
  bool Transitioning() const noexcept { return phase_ != Phase::Idle; }

  const Scene* Current() const noexcept { return scene_.get(); }
  std::uint64_t Generation() const noexcept { return generation_; }

private:
  enum class Phase { Idle, FadingOut, FadingIn };

  struct Environment {
    Color background;
    float fogStart;
    float fogEnd;
    Vector3 lightDirection;
    float ambient;
  };

  void ApplyEnvironment(const Environment& env);

  RetroEngine& engine_;
  RetroCamera& camera_;
  std::vector<std::string> paths_;
  float fadeSeconds_;

  Environment defaults_{};
  std::unique_ptr<Scene> scene_;
  std::size_t index_ = 0;
  std::size_t pending_ = 0;
  std::uint64_t generation_ = 0;
  Phase phase_ = Phase::Idle;
  float fade_ = 0.0f; // 0 = transparente, 1 = negro total
};

} // namespace retro