#pragma once

#include "assets/AssetManager.hpp"

#include <memory>
#include <random>
#include <string>
#include <unordered_set>
#include <vector>

namespace retro {

// Efectos, dos canales de streaming (música y ambiente, con fundido cruzado) y pasos.
// Si no hay dispositivo de audio, todo se ignora y el juego sigue sin sonido.
class AudioSystem {
public:
  AudioSystem(AssetManager& assets, bool enabled);

  AudioSystem(const AudioSystem&) = delete;
  AudioSystem& operator=(const AudioSystem&) = delete;

  bool Enabled() const noexcept { return enabled_; }

  // Cada frame: alimenta los streams y aplica los fundidos.
  void Update(float dt);

  void SetMasterVolume(float volume);
  float MasterVolume() const noexcept { return master_; }

  // Efecto puntual. 'volume' 0..1.
  void PlaySfx(const std::string& file, float volume = 1.0f);

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

  void StartChannel(Channel& channel, const std::string& file, float volume, float fade);
  static void UpdateChannel(Channel& channel, float dt);
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
};

} // namespace retro