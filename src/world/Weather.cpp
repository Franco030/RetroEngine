#include "world/Weather.hpp"

#include <algorithm>
#include <cmath>

namespace retro {

namespace {

constexpr int kDrops = 1400;      // con la lluvia a 1
constexpr float kRadius = 16.0f;  // las gotas caen en un cuadrado de 2R alrededor de la camara
constexpr float kHeight = 14.0f;  // y en una columna de esta altura
constexpr float kFall = 17.0f;    // metros por segundo
constexpr Vector3 kSlant{0.10f, 0.0f, 0.04f}; // el viento las tuerce un poco

// Modulo siempre positivo: la gota queda fija en el mundo aunque la camara se mueva.
float Wrap(float v, float size) {
  const float r = std::fmod(v, size);
  return r < 0.0f ? r + size : r;
}

// La forma del destello: dos golpes de luz seguidos y la cola, como un relampago de verdad.
float FlashShape(float t) {
  if (t < 0.0f)
    return 0.0f;
  if (t < 0.07f)
    return 1.0f;
  if (t < 0.13f)
    return 0.25f;
  if (t < 0.22f)
    return 0.85f;
  return std::max(0.0f, 0.6f * std::exp(-(t - 0.22f) * 7.0f));
}

} // namespace

Weather::Weather(RetroEngine& engine) : engine_(engine), audio_(engine.Audio()), retro_(engine.Retro()) {
  std::uniform_real_distribution<float> u(0.0f, 1.0f);
  drops_.reserve(kDrops);
  for (int i = 0; i < kDrops; ++i)
    drops_.push_back({u(rng_) * 2 * kRadius, u(rng_) * 2 * kRadius, u(rng_), 0.35f + 0.35f * u(rng_)});
}

void Weather::Apply(const std::optional<Scene::Weather>& config) {
  untilThunder_.reset();
  fog_.reset();
  ambientFade_.reset();
  flashTime_ = -1.0f;
  flash_ = 0.0f;
  photoTime_ = -1.0f;
  photo_ = 0.0f;
  baseAmbient_ = retro_.Params().ambient;
  baseFogStart_ = retro_.Params().fogStart;
  baseFogEnd_ = retro_.Params().fogEnd;
  baseBackground_ = shownBackground_ = engine_.Config().clearColor;
  baseMist_ = retro_.Params().mistDensity;
  const Scene::Weather w = config.value_or(Scene::Weather{});
  bank_ = w.fogBank;
  bankTime_ = -1.0f;
  calm_ = false;
  ScheduleBank();
  sound_ = w.sound;
  volume_ = w.volume;
  indoor_ = w.indoor;
  rain_ = rainTarget_ = w.rain;
  thunderMin_ = w.thunderMin;
  thunderMax_ = w.thunderMax;
  thunder_ = w.thunder;
  storm_ = thunderMin_ > 0.0f;
  ScheduleNext();
  audio_.PlayWeather(sound_, sound_.empty() ? 0.0f : volume_ * rain_, 1.5f);
}

void Weather::ApplyWind(const std::optional<Scene::Wind>& wind) {
  wind_ = wind;
  gustTime_ = -1.0f;
  gust_ = 0.0f;
  if (wind_ && wind_->gustMax > 0.0f) {
    std::uniform_real_distribution<float> u(wind_->gustMin, wind_->gustMax);
    untilGust_ = u(rng_);
  }
}

Vector2 Weather::WindPush(Vector3 feet, const Scene& scene, GameState& state, float dt) {
  if (!wind_) {
    gust_ = 0.0f;
    return {0.0f, 0.0f};
  }
  const Scene::Wind& w = *wind_;
  // la racha: sube en 0.4 s, sopla, y se apaga en 0.8 s
  if (w.gustMax > 0.0f) {
    if (gustTime_ < 0.0f) {
      untilGust_ -= dt;
      if (untilGust_ <= 0.0f) {
        gustTime_ = 0.0f;
        if (!w.gustSound.empty())
          audio_.PlaySfx(w.gustSound, w.gustVolume);
      }
    } else {
      gustTime_ += dt;
      if (gustTime_ > w.gustSeconds) {
        gustTime_ = -1.0f;
        std::uniform_real_distribution<float> u(w.gustMin, w.gustMax);
        untilGust_ = u(rng_);
      }
    }
  }
  gust_ = 0.0f;
  if (gustTime_ >= 0.0f) {
    const float in = std::min(1.0f, gustTime_ / 0.4f);
    const float out = std::min(1.0f, (w.gustSeconds - gustTime_) / 0.8f);
    gust_ = std::clamp(std::min(in, out), 0.0f, 1.0f);
  }
  state.Set("racha", gust_ > 0.3f ? 1 : 0);

  // hacia donde: desde el centro (o hacia el)
  Vector2 d{feet.x - w.center.x, feet.z - w.center.y};
  const float len = std::hypot(d.x, d.y);
  if (len < 1e-3f)
    return {0.0f, 0.0f};
  d = {d.x / len, d.y / len};
  if (!w.outward)
    d = {-d.x, -d.y};
  windDir_ = d;
  // a cubierto: algo solido entre tu pecho y de donde viene el viento
  const Vector3 chest{feet.x, feet.y + 1.1f, feet.z};
  const bool sheltered = w.shelter > 0.0f && scene.Blocked(chest, Vector3{-d.x, 0.0f, -d.y}, w.shelter);
  state.Set("a_cubierto", sheltered ? 1 : 0);
  if (sheltered)
    return {0.0f, 0.0f};
  const float s = w.strength + w.gustStrength * gust_;
  return {d.x * s, d.y * s};
}

void Weather::SetRain(float target, float fadeSeconds) {
  rainTarget_ = std::clamp(target, 0.0f, 1.0f);
  rainRate_ = std::fabs(rainTarget_ - rain_) / std::max(fadeSeconds, 0.01f);
  if (!sound_.empty())
    audio_.FadeWeather(volume_ * rainTarget_, fadeSeconds);
}

void Weather::SetStorm(bool on) {
  storm_ = on && thunderMin_ > 0.0f;
  ScheduleNext();
  // sin tormenta tampoco hay bancos de niebla: el que estuviera, se va ya
  calm_ = !on;
  if (calm_ && bank_ && bankTime_ >= 0.0f)
    bankTime_ = std::max(bankTime_, 2.0f * bank_->fade + bank_->seconds - bank_->fade * BankAmount());
  if (!calm_ && bankTime_ < 0.0f)
    ScheduleBank();
}

void Weather::Strike(float thunderDelay) {
  flashTime_ = 0.0f;
  if (!thunder_.empty())
    untilThunder_ = thunderDelay;
}

void Weather::PhotoFlash() { photoTime_ = 0.0f; }

void Weather::FadeAmbient(float value, float seconds) {
  ambientFade_ = AmbientFade{baseAmbient_, std::clamp(value, 0.0f, 1.0f), std::max(seconds, 0.0f), 0.0f};
}

void Weather::FadeFog(float start, float end, float seconds) {
  fog_ = FogFade{baseFogStart_, baseFogEnd_, start, end, std::max(seconds, 0.0f), 0.0f};
}

void Weather::ScheduleNext() {
  if (!storm_)
    return;
  std::uniform_real_distribution<float> u(thunderMin_, thunderMax_);
  untilStrike_ = u(rng_);
}

void Weather::ScheduleBank() {
  if (!bank_)
    return;
  std::uniform_real_distribution<float> u(bank_->everyMin, bank_->everyMax);
  untilBank_ = u(rng_);
}

float Weather::BankAmount() const noexcept {
  if (!bank_ || bankTime_ < 0.0f)
    return 0.0f;
  const float fade = std::max(bank_->fade, 0.001f);
  float k = 1.0f;
  if (bankTime_ < fade)                                         // entrando
    k = bankTime_ / fade;
  else if (bankTime_ > fade + bank_->seconds)                   // yendose
    k = std::max(0.0f, 1.0f - (bankTime_ - fade - bank_->seconds) / fade);
  return k * k * (3.0f - 2.0f * k);
}

void Weather::Update(float dt, GameState& state) {
  time_ += dt;

  if (rain_ < rainTarget_)
    rain_ = std::min(rainTarget_, rain_ + rainRate_ * dt);
  else if (rain_ > rainTarget_)
    rain_ = std::max(rainTarget_, rain_ - rainRate_ * dt);

  if (storm_) {
    untilStrike_ -= dt;
    if (untilStrike_ <= 0.0f) {
      // de lejos o de cerca: el trueno tarda lo que tarde en llegar
      std::uniform_real_distribution<float> delay(0.3f, 2.4f);
      Strike(delay(rng_));
      ScheduleNext();
    }
  }

  if (flashTime_ >= 0.0f) {
    flashTime_ += dt;
    flash_ = FlashShape(flashTime_);
    if (flashTime_ > 1.2f)
      flashTime_ = -1.0f;
  } else {
    flash_ = 0.0f;
  }
  // Durante el destello (lo bastante fuerte para ver), lo que solo esta en el relampago se ve.
  state.Set(kLightningFlag, flash_ > 0.2f ? 1 : 0);

  if (photoTime_ >= 0.0f) {
    photoTime_ += dt;
    photo_ = photoTime_ < 0.06f ? 1.0f : std::max(0.0f, std::exp(-(photoTime_ - 0.06f) * 9.0f));
    if (photoTime_ > 0.8f)
      photoTime_ = -1.0f;
  } else {
    photo_ = 0.0f;
  }

  if (untilThunder_) {
    *untilThunder_ -= dt;
    if (*untilThunder_ <= 0.0f) {
      std::uniform_int_distribution<std::size_t> pick(0, thunder_.size() - 1);
      std::uniform_real_distribution<float> loud(0.9f, 1.0f);
      audio_.PlaySfx(thunder_[pick(rng_)], loud(rng_) * (indoor_ ? 0.8f : 1.0f));
      untilThunder_.reset();
    }
  }

  if (fog_) {
    fog_->t += dt;
    const float k = fog_->seconds > 0.0f ? std::min(1.0f, fog_->t / fog_->seconds) : 1.0f;
    const float sm = k * k * (3.0f - 2.0f * k);
    baseFogStart_ = fog_->fromStart + (fog_->toStart - fog_->fromStart) * sm;
    baseFogEnd_ = fog_->fromEnd + (fog_->toEnd - fog_->fromEnd) * sm;
    if (k >= 1.0f)
      fog_.reset();
  }

  if (bank_) {
    if (bankTime_ < 0.0f) {
      if (!calm_)
        untilBank_ -= dt;
      if (untilBank_ <= 0.0f && !calm_) {
        bankTime_ = 0.0f;
        if (!bank_->sound.empty())
          audio_.PlaySfx(bank_->sound, bank_->volume);
      }
    } else {
      bankTime_ += dt;
      if (bankTime_ > 2.0f * bank_->fade + bank_->seconds) {
        bankTime_ = -1.0f;
        ScheduleBank();                                         // (en calma, espera a que vuelva)
      }
    }
  }

  if (ambientFade_) {
    ambientFade_->t += dt;
    const float k = ambientFade_->seconds > 0.0f ? std::min(1.0f, ambientFade_->t / ambientFade_->seconds) : 1.0f;
    baseAmbient_ = ambientFade_->from + (ambientFade_->to - ambientFade_->from) * k;
    if (k >= 1.0f)
      ambientFade_.reset();
  }

  // Durante el destello sube la luz ambiente (lo que hay se ve con su forma, no recortado en negro)
  // y la niebla se abre: en una escena a oscuras y con niebla cerrada, el relampago (o el flash)
  // deja ver lejos un instante.
  // (el flash de la camara sube poco la luz ambiente y abre algo la niebla: su luz hace el resto)
  // El banco de niebla cierra la niebla de la escena (nunca la abre) y la vuelve de su color; el
  // fondo cambia con ella (si no, lo de lejos se veria gris contra un cielo negro).
  const float b = BankAmount();
  const auto mix = [b](float from, float to) { return from + (to - from) * b; };
  const float bankStart = bank_ ? std::min(baseFogStart_, mix(baseFogStart_, bank_->start)) : baseFogStart_;
  const float bankEnd = bank_ ? std::min(baseFogEnd_, mix(baseFogEnd_, bank_->end)) : baseFogEnd_;
  const auto channel = [&](unsigned char from, unsigned char to) {
    return static_cast<unsigned char>(std::lround(mix(static_cast<float>(from), static_cast<float>(to))));
  };
  const Color background = bank_ ? Color{channel(baseBackground_.r, bank_->color.r), channel(baseBackground_.g, bank_->color.g),
                                         channel(baseBackground_.b, bank_->color.b), 255}
                                 : baseBackground_;
  if (background.r != shownBackground_.r || background.g != shownBackground_.g || background.b != shownBackground_.b) {
    engine_.SetClearColor(background); // (tambien el color de la niebla)
    shownBackground_ = background;
  }

  const float f = std::max(Lightning(), photo_ * 0.6f);
  RetroShaderParams p = retro_.Params();
  const float ambient = std::min(1.0f, baseAmbient_ + 0.9f * Lightning() + 0.12f * photo_);
  const float fogStart = bankStart + f * 12.0f;
  const float fogEnd = std::max(bankEnd, bankEnd + f * (60.0f - bankEnd * 0.2f));
  const float mist = bank_ ? std::max(baseMist_, mix(baseMist_, bank_->mist)) : baseMist_;
  if (p.ambient != ambient || p.fogStart != fogStart || p.fogEnd != fogEnd || p.mistDensity != mist) {
    p.ambient = ambient;
    p.fogStart = fogStart;
    p.fogEnd = fogEnd;
    p.mistDensity = mist;
    retro_.SetParams(p);
  }
}

void Weather::Draw(Vector3 cam) const {
  // en la racha, el aire se ve: rayas cortas y palidas, cerca del suelo, que corren con el viento
  if (gust_ > 0.05f && !indoor_) {
    const int count = static_cast<int>(110.0f * gust_);
    const float side = 2 * kRadius;
    const Color c{196, 202, 212, static_cast<unsigned char>(40 + 70 * gust_)};
    for (int i = 0; i < count; ++i) {
      const Drop& d = drops_[static_cast<std::size_t>(i)];
      // cada raya avanza con el viento y vuelve a empezar (en su cuadro alrededor de la camara)
      const float run = Wrap(time_ * 22.0f * (0.7f + d.length) + d.phase * side, side);
      const float x = cam.x - kRadius + Wrap(d.x + windDir_.x * run - (cam.x - kRadius), side);
      const float z = cam.z - kRadius + Wrap(d.z + windDir_.y * run - (cam.z - kRadius), side);
      const float y = cam.y - 1.5f + d.phase * 2.2f;
      const float largo = 0.5f + 0.9f * d.length;
      DrawLine3D({x, y, z}, {x + windDir_.x * largo, y - 0.02f, z + windDir_.y * largo}, c);
    }
  }
  if (indoor_ || rain_ <= 0.001f)
    return;
  const int count = static_cast<int>(static_cast<float>(drops_.size()) * rain_);
  const float side = 2 * kRadius;
  const Color color{120, 130, 150, static_cast<unsigned char>(90 + 60 * rain_)};
  const Color lit{220, 226, 240, 220};
  const Color c = flash_ > 0.2f ? lit : color;
  for (int i = 0; i < count; ++i) {
    const Drop& d = drops_[static_cast<std::size_t>(i)];
    const float x = cam.x - kRadius + Wrap(d.x - (cam.x - kRadius), side);
    const float z = cam.z - kRadius + Wrap(d.z - (cam.z - kRadius), side);
    const float fall = Wrap(time_ * kFall + d.phase * kHeight, kHeight);
    const float y = cam.y + kHeight * 0.5f - fall;
    const Vector3 top{x, y, z};
    const Vector3 bottom{x - kSlant.x * d.length * 4, y - d.length, z - kSlant.z * d.length * 4};
    DrawLine3D(top, bottom, c);
  }
}

} // namespace retro
