#pragma once

#include "assets/Resources.hpp"
#include "graphics/Light.hpp"

#include <raylib.h>

#include <memory>
#include <span>

namespace retro {

struct RetroShaderParams {
  Vector2 snapResolution{320.0f, 240.0f};
  float colorLevels = 32.0f;
  float ditherStrength = 1.0f;
  float affineAmount = 0.35f;
  Vector3 lightDirection{-0.5f, -1.0f, -0.3f};
  float ambient = 0.35f;

  Vector3 fogColor{0.094f, 0.078f, 0.125f};
  float fogStart = 10.0f;
  float fogEnd = 26.0f;
  bool fogEnabled = true;
  bool pointLights = true;
};

class RetroShader {
public:
  RetroShader(std::shared_ptr<ShaderResource> lit, std::shared_ptr<ShaderResource> unlit,
              std::shared_ptr<ShaderResource> sprite, const RetroShaderParams& params = {});

  void SetParams(const RetroShaderParams& params);
  const RetroShaderParams& Params() const noexcept { return params_; }

  void SetPointLights(std::span<const PointLight> lights);

  const std::shared_ptr<ShaderResource>& Lit() const noexcept { return lit_.shader; }
  const std::shared_ptr<ShaderResource>& Unlit() const noexcept { return unlit_.shader; }
  const std::shared_ptr<ShaderResource>& Sprite() const noexcept { return sprite_.shader; }

private:
  struct Locations {
    int snapResolution = -1;
    int colorLevels = -1;
    int dither = -1;
    int affine = -1;
    int lightDir = -1;
    int ambient = -1;
    int fogColor = -1;
    int fogStart = -1;
    int fogEnd = -1;
    int useLighting = -1;
    int lightCount = -1;
    int lightPos = -1;
    int lightColor = -1;
  };
  struct Program {
    std::shared_ptr<ShaderResource> shader;
    Locations loc;
  };

  static Program MakeProgram(std::shared_ptr<ShaderResource> shader);
  void Upload(const Program& program) const;

  Program lit_;
  Program unlit_;
  Program sprite_;
  RetroShaderParams params_;
};

} // namespace retro