#pragma once

#include "audio/Emitter.hpp"
#include "graphics/Light.hpp"
#include "world/Scene.hpp"

#include <raylib.h>

#include <nlohmann/json_fwd.hpp>

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <future>
#include <memory>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

namespace retro {

class RetroEngine;
class RetroCamera;

// Un modelo o una textura que usa una escena (para la precarga).
struct AssetRef {
  enum class Kind { Model, Texture, Sound };
  Kind kind;
  std::string path;
};

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

  // Precarga de las escenas vecinas (a las que lleva algun goto_scene de la actual). Un hilo de
  // trabajo lee y parsea sus JSON (PlanPrefetch); cuando termina, StepPrefetch retiene lo que ya
  // esta en memoria, pide lo que falta a los hilos y sube a la GPU poco a poco, con un
  // presupuesto por fotograma. Los JSON parseados se guardan: al ir a una vecina no se parsea.
  struct SceneDoc {
    std::shared_ptr<const nlohmann::json> json;
    std::filesystem::file_time_type stamp; // si el archivo cambia (F5), se vuelve a leer
  };
  struct PrefetchPlan {
    std::vector<std::string> neighbors;
    std::unordered_map<std::string, SceneDoc> docs;
    std::vector<AssetRef> items;
  };
  void PlanPrefetch(std::shared_ptr<const nlohmann::json> current);
  void StepPrefetch();
  // El JSON de 'path': el guardado si sigue al dia, si no lo lee ahora.
  std::shared_ptr<const nlohmann::json> SceneJson(const std::string& path);

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

  std::future<PrefetchPlan> plan_;
  bool purgePending_ = false;  // la purga espera al plan (para no soltar lo que usan las vecinas)
  std::unordered_map<std::string, SceneDoc> docs_; // JSON de las vecinas, ya parseados
  std::vector<AssetRef> prefetch_;             // pedidos a los hilos, por retener al subirse
  std::vector<std::shared_ptr<ModelResource>> keepModels_;    // de las escenas vecinas
  std::vector<std::shared_ptr<TextureResource>> keepTextures_;
  std::vector<std::shared_ptr<SoundResource>> keepSounds_;
  double prefetchMs_ = 0.0;     // tiempo de la precarga en curso (para el informe)
  double prefetchMaxMs_ = 0.0;  // el fotograma que mas le dedico
  int prefetchFrames_ = 0;
  int prefetchCount_ = 0;
};

} // namespace retro