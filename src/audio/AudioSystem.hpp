#pragma once

#include "assets/AssetManager.hpp"
#include "audio/Emitter.hpp"

#include <atomic>
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

// Efectos, dos canales de streaming (música y ambiente, con fundido cruzado) y pasos.
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
  void PlayMusic(const std::string& file, float volume, float fadeSeconds);
  void StopMusic(float fadeSeconds) { PlayMusic("", 0.0f, fadeSeconds); }
  void PlayAmbient(const std::string& file, float volume, float fadeSeconds);

  void SetFootsteps(const std::vector<std::string>& files, float volume);
  void Footstep();

private:
  struct Channel {
    std::unique_ptr<MusicResource> current;
    std::unique_ptr<MusicResource> outgoing;
    std::string file;
    float volume = 0.0f;
    float target = 0.0f;
    float upRate = 1.0f;
    float outVolume = 0.0f;
    float outRate = 1.0f;
  };

  struct Voice {
    std::unique_ptr<MusicResource> stream;
    float volume = 0.0f;
    float target = 0.0f;
    float pan = 0.5f;
  };

  float Spatialize(Vector3 position, float volume, float radius, float minDistance,
                   float& pan) const;

  Vector3 listenerPos_{0.0f, 0.0f, 0.0f};
  Vector3 listenerFwd_{0.0f, 0.0f, -1.0f};
  std::unordered_map<std::string, Voice> voices_;
  unsigned fadingSerial_ = 0; // para renombrar los emisores de la escena anterior

  void StartChannel(Channel& channel, const std::string& file, float volume, float fade);
  static void UpdateChannel(Channel& channel, float dt);
  void FeedLoop(); // el hilo: UpdateMusicStream de todos los streams
  void Warn(const std::string& file, const char* what);

  AssetManager& assets_;
  bool enabled_;
  float master_ = 1.0f;

  Channel music_;
  Channel ambient_;

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