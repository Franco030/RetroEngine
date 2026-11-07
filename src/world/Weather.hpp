#pragma once

#include "core/RetroEngine.hpp"
#include "game/GameState.hpp"
#include "world/Scene.hpp"

#include <raylib.h>

#include <algorithm>
#include <optional>
#include <random>
#include <string>
#include <vector>

namespace retro {

// El tiempo de la escena (su "weather"): la lluvia que cae alrededor de la camara y su sonido, los
// relampagos (el destello, la bandera "relampago" y el trueno despues), los cambios de niebla y
// los bancos de niebla (la niebla que entra de golpe, se cierra un rato y se va).
// Lo lleva main: al cambiar de escena, Apply; cada fotograma, Update y, dentro del 3D, Draw.
class Weather {
public:
  static constexpr const char* kLightningFlag = "relampago";

  explicit Weather(RetroEngine& engine);

  // La escena nueva: su tiempo de golpe (sin "weather", ni lluvia ni tormenta). Corta lo que
  // estuviera pasando (un trueno pendiente, una niebla a medio cambiar).
  void Apply(const std::optional<Scene::Weather>& config);

  // El viento que empuja de la escena (o ninguno). Corta la racha que hubiera.
  void ApplyWind(const std::optional<Scene::Wind>& wind);
  // Cada fotograma: lo que empuja al jugador (m/s, en x y z), con los pies en 'feet'. Lleva las
  // rachas y pone las banderas "racha" (1 mientras sopla una) y "a_cubierto" (1 detras de algo).
  Vector2 WindPush(Vector3 feet, const Scene& scene, GameState& state, float dt);

  // Acciones "weather", "lightning" y "fog".
  void SetRain(float target, float fadeSeconds);
  void SetStorm(bool on);
  void Strike(float thunderDelay);
  void FadeFog(float start, float end, float seconds);
  // Accion "ambient": la luz ambiente de la escena pasa a 'value' en 'seconds'.
  void FadeAmbient(float value, float seconds);
  // El flash de la camara: el mismo destello, mas corto, sin bandera ni trueno.
  void PhotoFlash();

  void Update(float dt, GameState& state);
  // Las gotas, dentro del 3D (lineas con el shader por defecto).
  void Draw(Vector3 cameraPosition) const;

  // El destello, 0..1, para el postproceso: el del relampago (mas flojo bajo techo) aclara toda la
  // imagen; el flash de la camara apenas (lo que alumbra es su luz, ver Photo).
  float Flash() const noexcept { return std::max(Lightning(), photo_ * 0.22f); }
  // El flash de la camara, 0..1: main pone una luz en la camara con esta fuerza.
  float Photo() const noexcept { return photo_; }
  // Cuanto tapa ahora el banco de niebla (0..1): el panorama se funde con la niebla. Va por delante
  // de la niebla (el doble de deprisa): si no, al entrar, lo de lejos (el acantilado) se aclara antes
  // que el cielo y se recorta contra el.
  float Haze() const noexcept { return std::min(1.0f, 2.0f * BankAmount()); }

private:
  float Lightning() const noexcept { return flash_ * (indoor_ ? 0.45f : 1.0f); }

  struct Drop {
    float x, z, phase, length;
  };
  struct FogFade {
    float fromStart, fromEnd, toStart, toEnd, seconds, t;
  };

  void ScheduleNext();
  void ScheduleBank();
  // 0..1: cuanto ha entrado el banco de niebla ahora (0 sin banco).
  float BankAmount() const noexcept;

  RetroEngine& engine_;
  AudioSystem& audio_;
  RetroShader& retro_;
  std::mt19937 rng_{std::random_device{}()};
  std::vector<Drop> drops_;

  std::string sound_;
  float volume_ = 0.0f;
  bool indoor_ = false;
  float rain_ = 0.0f;       // la que cae ahora
  float rainTarget_ = 0.0f;
  float rainRate_ = 1.0f;   // unidades por segundo hacia rainTarget_

  bool storm_ = false;
  float thunderMin_ = 0.0f;
  float thunderMax_ = 0.0f;
  std::vector<std::string> thunder_;
  float untilStrike_ = 0.0f;
  std::optional<float> untilThunder_;
  float flashTime_ = -1.0f; // segundos desde el ultimo relampago (negativo: ninguno)
  float flash_ = 0.0f;
  float time_ = 0.0f;
  float photoTime_ = -1.0f;
  float photo_ = 0.0f;
  // La luz ambiente de la escena: durante el destello se sube (lo de dentro del relampago se ve
  // con su forma, no recortado en negro) y luego vuelve a esta.
  float baseAmbient_ = 0.35f;
  // y la niebla de la escena (la que cambia la accion "fog"): el destello la abre un instante.
  float baseFogStart_ = 10.0f;
  float baseFogEnd_ = 26.0f;

  std::optional<FogFade> fog_;
  struct AmbientFade {
    float from, to, seconds, t;
  };
  std::optional<AmbientFade> ambientFade_;

  // el viento que empuja
  std::optional<Scene::Wind> wind_;
  float untilGust_ = 0.0f;
  float gustTime_ = -1.0f;  // segundos desde que empezo la racha (negativo: no hay)
  float gust_ = 0.0f;       // 0..1, cuanto sopla la racha ahora
  Vector2 windDir_{0.0f, 0.0f};

  // el banco de niebla: la de la escena (color de fondo y neblina baja) es a la que vuelve
  std::optional<Scene::Weather::FogBank> bank_;
  float untilBank_ = 0.0f;
  float bankTime_ = -1.0f; // segundos desde que empezo a entrar (negativo: no hay)
  bool calm_ = false;      // la accion "weather" con storm false: ni relampagos ni bancos
  Color baseBackground_{24, 20, 32, 255};
  float baseMist_ = 0.0f;
  Color shownBackground_{0, 0, 0, 0};
};

} // namespace retro
