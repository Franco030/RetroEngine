#include "audio/AudioSystem.hpp"

#include <raylib.h>

#include <algorithm>
#include <iostream>
#include <utility>

namespace retro {

AudioSystem::AudioSystem(AssetManager& assets, bool enabled) : assets_(assets), enabled_(enabled) {}

void AudioSystem::Warn(const std::string& file, const char* what) {
  if (warned_.insert(file).second)
    std::cerr << "Audio: " << what << '\n';
}

void AudioSystem::SetMasterVolume(float volume) {
  master_ = std::clamp(volume, 0.0f, 1.0f);
  if (enabled_)
    ::SetMasterVolume(master_);
}

void AudioSystem::PlaySfx(const std::string& file, float volume) {
  if (!enabled_ || file.empty())
    return;
  try {
    const auto sound = assets_.GetSound(file);
    SetSoundVolume(sound->Get(), std::clamp(volume, 0.0f, 1.0f));
    SetSoundPitch(sound->Get(), 1.0f);
    PlaySound(sound->Get());
  } catch (const std::exception& e) {
    Warn(file, e.what());
  }
}

void AudioSystem::StartChannel(Channel& ch, const std::string& file, float volume, float fade) {
  if (!enabled_)
    return;

  if (file == ch.file) {
    ch.target = file.empty() ? 0.0f : volume;
    ch.upRate = std::max(volume, 0.1f);
    return;
  }

  std::unique_ptr<MusicResource> next;
  if (!file.empty()) {
    try {
      next = std::make_unique<MusicResource>(assets_.Root() + file);
    } catch (const std::exception& e) {
      Warn(file, e.what());
      return;
    }
  }

  const float f = std::max(fade, 0.01f);
  if (ch.current) {
    ch.outgoing = std::move(ch.current);
    ch.outVolume = ch.volume;
    ch.outRate = std::max(ch.volume, 0.05f) / f;
  }

  ch.file = file;
  ch.current = std::move(next);
  if (ch.current) {
    PlayMusicStream(ch.current->Get());
    ch.volume = fade > 0.0f ? 0.0f : volume;
    ch.target = volume;
    ch.upRate = std::max(volume, 0.1f) / f;
  }
}

void AudioSystem::UpdateChannel(Channel& ch, float dt) {
  if (ch.outgoing) {
    ch.outVolume -= ch.outRate * dt;
    if (ch.outVolume <= 0.0f) {
      StopMusicStream(ch.outgoing->Get());
      ch.outgoing.reset();
    } else {
      SetMusicVolume(ch.outgoing->Get(), ch.outVolume);
      UpdateMusicStream(ch.outgoing->Get());
    }
  }
  if (ch.current) {
    if (ch.volume < ch.target) {
      ch.volume = std::min(ch.target, ch.volume + ch.upRate * dt);
    } else if (ch.volume > ch.target) {
      ch.volume = std::max(ch.target, ch.volume - ch.upRate * dt);
    }
    SetMusicVolume(ch.current->Get(), ch.volume);
    UpdateMusicStream(ch.current->Get());
  }
}

void AudioSystem::PlayMusic(const std::string& file, float volume, float fade) {
  StartChannel(music_, file, std::clamp(volume, 0.0f, 1.0f), fade);
}

void AudioSystem::PlayAmbient(const std::string& file, float volume, float fade) {
  StartChannel(ambient_, file, std::clamp(volume, 0.0f, 1.0f), fade);
}

void AudioSystem::Update(float dt) {
  if (!enabled_)
    return;
  UpdateChannel(music_, dt);
  UpdateChannel(ambient_, dt);
}

void AudioSystem::SetFootsteps(const std::vector<std::string>& files, float volume) {
  footsteps_.clear();
  footstepVolume_ = std::clamp(volume, 0.0f, 1.0f);
  lastStep_ = -1;
  if (!enabled_)
    return;

  for (const std::string& f : files) {
    try {
      footsteps_.push_back(assets_.GetSound(f));
    } catch (const std::exception& e) {
      Warn(f, e.what());
    }
  }
}

void AudioSystem::Footstep() {
  if (!enabled_ || footsteps_.empty())
    return;

  std::uniform_int_distribution<int> pick(0, static_cast<int>(footsteps_.size()) - 1);
  int i = pick(rng_);
  if (footsteps_.size() > 1 && i == lastStep_)
    i = (i + 1) % static_cast<int>(footsteps_.size());
  lastStep_ = i;

  std::uniform_real_distribution<float> var(-1.0f, 1.0f);
  const Sound& s = footsteps_[static_cast<std::size_t>(i)]->Get();
  SetSoundVolume(s, std::clamp(footstepVolume_ * (1.0f + 0.12f * var(rng_)), 0.0f, 1.0f));
  SetSoundPitch(s, 1.0f + 0.08f * var(rng_));
  PlaySound(s);
}

} // namespace retro