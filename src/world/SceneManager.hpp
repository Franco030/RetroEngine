#pragma once

#include "audio/Emitter.hpp"
#include "graphics/Light.hpp"
#include "world/Scene.hpp"

#include <raylib.h>

#include <cstddef>
#include <cstdint>
#include <deque>
#include <memory>
#include <optional>
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
  // Empieza con la pantalla en negro y la aclara (al arrancar el juego): mientras esta oscura,
  // la precarga de las escenas vecinas carga lo pesado sin que se note.
  void FadeInFromBlack();

  // Cambia a la escena 'relativePath' con fundido. Si no está en la lista y el
  // archivo existe, se añade. spawnPosition/spawnYaw reemplazan el spawn del
  // JSON de destino. Lanza std::invalid_argument si el archivo no existe.
  void GoToPath(const std::string& relativePath,
                std::optional<Vector3> spawnPosition = std::nullopt,
                std::optional<float> spawnYaw = std::nullopt);

  void GoTo(std::size_t index);
  void Next();
  void Previous();

  void Reload();

  void Update(float dt);
  void UpdateAudio(float dt);
  void Draw();
  void DrawFade() const;

  std::size_t Index() const noexcept { return index_; }
  std::size_t Count() const noexcept { return paths_.size(); }
  const std::string& CurrentPath() const noexcept { return paths_[index_]; }
  bool Transitioning() const noexcept { return phase_ != Phase::Idle; }

  const Scene* Current() const noexcept { return scene_.get(); }
  Scene* CurrentMut() noexcept { return scene_.get(); }
  std::uint64_t Generation() const noexcept { return generation_; }

  void SetFocus(Vector3 focus) noexcept { focus_ = focus; }

private:
  enum class Phase { Idle, FadingOut, FadingIn };

  struct Environment {
    Color background;
    float fogStart;
    float fogEnd;
    Vector3 lightDirection;
    float ambient;
    float sun;
    float mistDensity;
    float mistHeight;
    Vector3 mistColor;
  };

  Vector3 focus_{0.0f, 0.0f, 0.0f};

  void ApplyEnvironment(const Environment& env);
  void ApplyAudio(const SceneAudio& audio);

  // Precarga de las escenas vecinas (a las que lleva algun goto_scene de la actual): lo que ya
  // esta en memoria se retiene para que la purga no lo borre, y lo que falta se carga poco a
  // poco (StepPrefetch, con un presupuesto por fotograma) mientras se juega.
  void PlanPrefetch();
  void StepPrefetch();

  RetroEngine& engine_;
  RetroCamera& camera_;
  std::vector<std::string> paths_;
  float fadeSeconds_;

  std::optional<Vector3> pendingSpawnPos_;
  std::optional<float> pendingSpawnYaw_;

  std::vector<PointLight> lights_;
  std::vector<Emitter> emitters_;

  Environment defaults_{};
  std::unique_ptr<Scene> scene_;
  std::size_t index_ = 0;
  std::size_t pending_ = 0;
  std::uint64_t generation_ = 0;
  Phase phase_ = Phase::Idle;
  float fade_ = 0.0f; // 0 = transparente, 1 = negro total

  struct PrefetchItem {
    bool model; // si no, textura
    std::string path;
  };
  std::deque<PrefetchItem> prefetch_;
  std::vector<std::shared_ptr<ModelResource>> keepModels_;    // de las escenas vecinas
  std::vector<std::shared_ptr<TextureResource>> keepTextures_;
  double prefetchMs_ = 0.0;     // tiempo de la precarga en curso (para el informe)
  double prefetchMaxMs_ = 0.0;  // el fotograma que mas le dedico
  int prefetchFrames_ = 0;
  int prefetchCount_ = 0;
};

} // namespace retro