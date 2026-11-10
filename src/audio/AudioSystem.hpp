#pragma once

#include "assets/AssetManager.hpp"
#include "audio/Emitter.hpp"

#include <atomic>
#include <algorithm>
#include <memory>
#include <mutex>
#include <random>
#include <span>
#include <string>
#include <thread>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace retro {

// Efectos, tres canales de streaming (música, ambiente y lluvia, con fundido cruzado) y pasos.
// Si no hay dispositivo de audio, todo se ignora y el juego sigue sin sonido.
//
// Los streams (musica, ambiente y emisores) los alimenta un HILO PROPIO cada pocos milisegundos:
// si un fotograma tarda (un cambio de escena, una carga), la musica no se corta. Todo lo que toca
// los streams va bajo mutex_; los metodos publicos lo toman y el hilo tambien.
class AudioSystem {
public:
  AudioSystem(AssetManager& assets, bool enabled);
  ~AudioSystem();

  AudioSystem(const AudioSystem&) = delete;
  AudioSystem& operator=(const AudioSystem&) = delete;

  bool Enabled() const noexcept { return enabled_; }

  // Cada frame: alimenta los streams y aplica los fundidos.
  void Update(float dt);

  void SetMasterVolume(float volume);
  float MasterVolume() const noexcept { return master_; }

  // Efecto puntual. 'volume' 0..1.
  void PlaySfx(const std::string& file, float volume = 1.0f);

  void SetListener(Vector3 position, Vector3 forward);
  void UpdateEmitters(std::span<const Emitter> emitters, float dt);
  void ClearEmitters();

  void PlaySfxAt(const std::string& file, float volume, Vector3 position, float radius,
                 float minDistance = 1.0f);
  // Con 'then', 'file' suena una sola vez y al acabar empieza 'then' en bucle (una entrada y
  // lo que se queda sonando). Pedir 'then' mientras aun suena la entrada no la corta.
  void PlayMusic(const std::string& file, float volume, float fadeSeconds,
                 const std::string& then = {});
  void StopMusic(float fadeSeconds) { PlayMusic("", 0.0f, fadeSeconds); }
  // Varias canciones al azar, como en Minecraft: suena una entera (sin bucle), luego un silencio
  // de gap.x a gap.y segundos (al azar) y otra: van todas antes de repetir ninguna (una bolsa), y
  // nunca la misma dos veces seguidas. Pedir la misma lista que ya suena no la corta. PlayMusic la quita.
  void PlayShuffle(const std::vector<std::string>& files, float volume, Vector2 gap, float fadeSeconds);
  void PlayAmbient(const std::string& file, float volume, float fadeSeconds);
  // La lluvia: un tercer canal en bucle, aparte de la musica y el ambiente, para que pueda
  // subir, bajar o cortarse de golpe ('FadeWeather', en 'seconds') sin tocar los otros dos.
  void PlayWeather(const std::string& file, float volume, float fadeSeconds);
  void FadeWeather(float volume, float seconds);

  // Bajo techo, con su propia musica (la tienda de la ciudad del cilindro): la musica y el ambiente
  // de la escena bajan a 'factor' (0 = nada) en unos 0.6 s, y vuelven igual. La lluvia y los
  // sonidos con posicion no cambian.
  void SetIndoor(float factor) noexcept { duckTarget_ = std::clamp(factor, 0.0f, 1.0f); }

  void SetFootsteps(const std::vector<std::string>& files, float volume);
  void Footstep();

private:
  struct Channel {
    std::unique_ptr<MusicResource> current;
    std::unique_ptr<MusicResource> outgoing;
    std::string file;
    std::string then; // lo que sigue a 'file' cuando acabe (vacio: 'file' en bucle)
    float volume = 0.0f;
    float target = 0.0f;
    float upRate = 1.0f;
    float outVolume = 0.0f;
    float outRate = 1.0f;
    // PlayShuffle: la lista, el silencio entre una y otra, lo que falta de el y la ultima que sono
    std::vector<std::string> shuffle;
    Vector2 gap{0.0f, 0.0f};
    float shuffleVolume = 0.0f;
    float wait = 0.0f;
    std::string last;
    std::vector<std::string> bag; // lo que falta por sonar de esta vuelta
  };
  // La siguiente de la bolsa: todas suenan una vez antes de repetir ninguna, y la vuelta nueva no
  // empieza con la que acaba de sonar.
  std::string PickShuffle(Channel& channel);

  struct Voice {
    std::unique_ptr<MusicResource> stream;
    float volume = 0.0f;
    float target = 0.0f;
    float pan = 0.5f;
  };

  float Spatialize(Vector3 position, float volume, float radius, float minDistance,
                   float& pan, bool directional = true) const;

  Vector3 listenerPos_{0.0f, 0.0f, 0.0f};
  Vector3 listenerFwd_{0.0f, 0.0f, -1.0f};
  std::unordered_map<std::string, Voice> voices_;
  unsigned fadingSerial_ = 0; // para renombrar los emisores de la escena anterior

  void StartChannel(Channel& channel, const std::string& file, float volume, float fade,
                    const std::string& then = {});
  static void UpdateChannel(Channel& channel, float dt, float factor = 1.0f);
  void FeedLoop(); // el hilo: UpdateMusicStream de todos los streams
  void Warn(const std::string& file, const char* what);

  AssetManager& assets_;
  bool enabled_;
  float master_ = 1.0f;

  float duck_ = 1.0f;       // lo que suenan ahora la musica y el ambiente (SetIndoor)
  float duckTarget_ = 1.0f;
  Channel music_;
  Channel ambient_;
  Channel weather_;

  std::vector<std::shared_ptr<SoundResource>> footsteps_;
  float footstepVolume_ = 0.6f;
  int lastStep_ = -1;

  std::mt19937 rng_{std::random_device{}()};
  std::unordered_set<std::string> warned_;

  // Al final: el hilo se crea cuando todo lo demas ya existe y se para antes de destruirlo.
  std::mutex mutex_;
  std::atomic<bool> feeding_{false};
  std::thread feeder_;
};

} // namespace retro