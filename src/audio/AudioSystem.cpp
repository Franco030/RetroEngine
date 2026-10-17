#include "audio/AudioSystem.hpp"

#include <raylib.h>
#include <raymath.h>

#include <algorithm>
#include <cmath>
#include <iostream>
#include <unordered_set>
#include <utility>

namespace retro {

namespace {

constexpr std::size_t kMaxEmitterVoices = 6;
constexpr bool kFlipPan = false;

} // namespace

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
    SetSoundPan(sound->Get(), 0.5f);
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
  SetSoundPan(s, 0.5f);
  PlaySound(s);
}

void AudioSystem::SetListener(Vector3 position, Vector3 forward) {
  listenerPos_ = position;
  const Vector3 flat{forward.x, 0.0f, forward.z};
  const float len = Vector3Length(flat);
  if (len > 1e-3f)
    listenerFwd_ = Vector3Scale(flat, 1.0f / len);
}

float AudioSystem::Spatialize(Vector3 position, float volume, float radius, float minDistance,
                              float& pan) const {
  pan = 0.5f;
  const Vector3 d = Vector3Subtract(position, listenerPos_);
  const float dist = Vector3Length(d);
  if (dist >= radius)
    return 0.0f;

  const float t =
      std::clamp((dist - minDistance) / std::max(radius - minDistance, 0.001f), 0.0f, 1.0f);
  float gain = volume * (1.0f - t) * (1.0f - t);

  const Vector3 flat{d.x, 0.0f, d.z};
  const float flatLen = Vector3Length(flat);
  if (flatLen > 1e-3f) {
    const Vector3 dir = Vector3Scale(flat, 1.0f / flatLen);
    const Vector3 right = {-listenerFwd_.z, 0.0f, listenerFwd_.x};
    const float lateral = Vector3DotProduct(dir, right) * std::clamp(flatLen / 2.0f, 0.0f, 1.0f);
    const float front = Vector3DotProduct(dir, listenerFwd_);
    if (front < 0.0f)
      gain *= 1.0f + 0.35f * front;
    pan = 0.5f - 0.5f * lateral * 0.85f * (kFlipPan ? -1.0f : 1.0f);
  }
  return gain;
}

void AudioSystem::PlaySfxAt(const std::string& file, float volume, Vector3 position, float radius,
                            float minDistance) {
  if (!enabled_ || file.empty())
    return;

  float pan = 0.5f;
  const float gain = Spatialize(position, volume, radius, minDistance, pan);
  if (gain <= 0.002f)
    return;

  try {
    const auto sound = assets_.GetSound(file);
    SetSoundVolume(sound->Get(), std::clamp(gain, 0.0f, 1.0f));
    SetSoundPan(sound->Get(), pan);
    SetSoundPitch(sound->Get(), 1.0f);
    PlaySound(sound->Get());
  } catch (const std::exception& e) {
    Warn(file, e.what());
  }
}

void AudioSystem::UpdateEmitters(std::span<const Emitter> emitters, float dt) {
  if (!enabled_)
    return;

  struct Candidate {
    const Emitter* emitter;
    float gain;
    float pan;
  };

  std::vector<Candidate> candidates;
  candidates.reserve(emitters.size());
  for (const Emitter& e : emitters) {
    float pan = 0.5f;
    const float gain = Spatialize(e.position, e.volume, e.radius, e.minDistance, pan);
    if (gain > 0.002f)
      candidates.push_back({&e, gain, pan});
  }
  std::sort(candidates.begin(), candidates.end(),
            [](const Candidate& a, const Candidate& b) { return a.gain > b.gain; });
  if (candidates.size() > kMaxEmitterVoices)
    candidates.resize(kMaxEmitterVoices);

  std::unordered_set<std::string> live;
  for (const Candidate& c : candidates) {
    live.insert(c.emitter->key);

    auto it = voices_.find(c.emitter->key);
    if (it == voices_.end()) {
      try {
        auto stream = std::make_unique<MusicResource>(assets_.Root() + c.emitter->file);
        PlayMusicStream(stream->Get());
        std::uniform_real_distribution<float> start(0.0f, 1.0f);
        SeekMusicStream(stream->Get(), start(rng_) * GetMusicTimeLength(stream->Get()));
        Voice voice;
        voice.stream = std::move(stream);
        it = voices_.emplace(c.emitter->key, std::move(voice)).first;
      } catch (const std::exception& ex) {
        Warn(c.emitter->file, ex.what());
        continue;
      }
    }
    it->second.target = c.gain;
    it->second.pan = c.pan;
  }

  const float k = std::min(1.0f, dt * 8.0f);
  for (auto it = voices_.begin(); it != voices_.end();) {
    Voice& v = it->second;
    if (live.find(it->first) == live.end())
      v.target = 0.0f;
    v.volume += (v.target - v.volume) * k;

    if (v.target == 0.0f && v.volume < 0.004f) {
      StopMusicStream(v.stream->Get());
      it = voices_.erase(it);
      continue;
    }
    SetMusicVolume(v.stream->Get(), std::clamp(v.volume, 0.0f, 1.0f));
    SetMusicPan(v.stream->Get(), v.pan);
    UpdateMusicStream(v.stream->Get());
    ++it;
  }
}

} // namespace retro