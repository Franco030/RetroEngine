#pragma once

#include "assets/Resources.hpp"

#include <raylib.h>

#include <memory>

namespace retro {

struct RetroShaderParams {
  Vector2 snapResolution{320.0f, 240.0f};
  float colorLevels = 32.0f;
  float ditherStrength = 1.0f;
  Vector3 lightDirection{-0.5f, -1.0f, -0.3f};
  float ambient = 0.35f;
};

class RetroShader {
public:
  explicit RetroShader(std::shared_ptr<ShaderResource> shader,
                       const RetroShaderParams& params = {});
  void SetParams(const RetroShaderParams& params);
  const RetroShaderParams& Params() const noexcept { return params_; }

  const std::shared_ptr<ShaderResource>& Resource() const noexcept { return shader_; }

private:
  void Upload() const;

  std::shared_ptr<ShaderResource> shader_;
  RetroShaderParams params_;

  int locSnapResolution_ = -1;
  int locColorLevels_ = -1;
  int locDither_ = -1;
  int locLightDir_ = -1;
  int locAmbient_ = -1;
};

} // namespace retro