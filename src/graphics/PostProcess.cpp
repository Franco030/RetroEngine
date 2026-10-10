#include "graphics/PostProcess.hpp"

#include "graphics/ShaderScope.hpp"

#include <stdexcept>
#include <utility>

namespace retro {

namespace {

void SetUniform(const Shader& s, int loc, const void* value, int type) {
  if (loc >= 0)
    SetShaderValue(s, loc, value, type);
}

} // namespace

PostProcess::PostProcess(std::shared_ptr<ShaderResource> shader, const PostParams& params)
    : shader_(std::move(shader)), params_(params) {
  if (!shader_ || !shader_->IsValid()) {
    throw std::invalid_argument("PostProcess: shader nulo o invalido");
  }

  const Shader& s = shader_->Get();
  locTexSize_ = GetShaderLocation(s, "texSize");
  locScale_ = GetShaderLocation(s, "scale");
  locFilter_ = GetShaderLocation(s, "filterMode");
  locVignette_ = GetShaderLocation(s, "vignette");
  locGrain_ = GetShaderLocation(s, "grain");
  locSaturation_ = GetShaderLocation(s, "saturation");
  locTint_ = GetShaderLocation(s, "tint");
  locTime_ = GetShaderLocation(s, "time");
}

void PostProcess::Draw(const Texture2D& source, Rectangle dst) const {
  const Shader& s = shader_->Get();

  const Vector2 texSize{static_cast<float>(source.width), static_cast<float>(source.height)};
  const Vector2 scale{dst.width / texSize.x, dst.height / texSize.y};
  const int mode = static_cast<int>(params_.filter);

  const bool fx = params_.effects;
  const float vignette = fx ? params_.vignette : 0.0f;
  const float grain = fx ? params_.grain : 0.0f;
  const float saturation = fx ? params_.saturation : 1.0f;
  const Vector3 tint = fx ? params_.tint : Vector3{1.0f, 1.0f, 1.0f};
  const float time = static_cast<float>(GetTime());

  SetUniform(s, locTexSize_, &texSize, SHADER_UNIFORM_VEC2);
  SetUniform(s, locScale_, &scale, SHADER_UNIFORM_VEC2);
  SetUniform(s, locFilter_, &mode, SHADER_UNIFORM_INT);
  SetUniform(s, locVignette_, &vignette, SHADER_UNIFORM_FLOAT);
  SetUniform(s, locGrain_, &grain, SHADER_UNIFORM_FLOAT);
  SetUniform(s, locSaturation_, &saturation, SHADER_UNIFORM_FLOAT);
  SetUniform(s, locTint_, &tint, SHADER_UNIFORM_VEC3);
  SetUniform(s, locTime_, &time, SHADER_UNIFORM_FLOAT);

  const Rectangle src{0.0f, 0.0f, texSize.x, -texSize.y};

  ShaderScope scope(s);
  DrawTexturePro(source, src, dst, {0.0f, 0.0f}, 0.0f, WHITE);
}

} // namespace retro