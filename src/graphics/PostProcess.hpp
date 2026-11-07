#pragma once

#include "assets/Resources.hpp"

#include <raylib.h>

#include <memory>

namespace retro {

enum class FilterMode : int { Nearest = 0, SharpBilinear = 1, Bilinear = 2 };

struct PostParams {
  FilterMode filter = FilterMode::SharpBilinear;
  float vignette = 0.30f;
  float grain = 0.035f;
  float saturation = 1.05f;
  Vector3 tint{1.04f, 1.0f, 0.93f};
  bool effects = true;
  // Ojos cerrados (no dependen de 'effects': es parte del juego). 'eyes' va de 0 (abiertos) a 1
  // (cerrados: la ciudad que duerme, en azul); a medio camino, los parpados en negro. 'drowsy':
  // cuanto falta para que gane el sueno (0..1).
  float eyes = 0.0f;
  float drowsy = 0.0f;
  // Un relampago o el flash de la camara: 0..1, todo se aclara y se enfria de golpe.
  float flash = 0.0f;
};

class PostProcess {
public:
  explicit PostProcess(std::shared_ptr<ShaderResource> shader, const PostParams& params = {});

  PostParams& Params() noexcept { return params_; }
  const PostParams& Params() const noexcept { return params_; }
  void SetParams(const PostParams& params) noexcept { params_ = params; }

  void Draw(const Texture2D& source, Rectangle dst) const;

private:
  std::shared_ptr<ShaderResource> shader_;
  PostParams params_;

  int locTexSize_ = -1, locScale_ = -1, locFilter_ = -1, locVignette_ = -1;
  int locGrain_ = -1, locSaturation_ = -1, locTint_ = -1, locTime_ = -1;
  int locEyes_ = -1, locDrowsy_ = -1, locFlash_ = -1;
};

} // namespace retro