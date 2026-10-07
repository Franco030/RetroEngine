#include "graphics/RetroShader.hpp"

#include <stdexcept>
#include <utility>

namespace retro {

RetroShader::RetroShader(std::shared_ptr<ShaderResource> shader, const RetroShaderParams& params)
    : shader_(std::move(shader)), params_(params) {
  if (!shader_ || !shader_->IsValid()) {
    throw std::invalid_argument("RetroShader: shader nulo o invalido");
  }

  const Shader& s = shader_->Get();
  locSnapResolution_ = GetShaderLocation(s, "snapResolution");
  locColorLevels_ = GetShaderLocation(s, "colorLevels");
  locDither_ = GetShaderLocation(s, "ditherStrength");
  locLightDir_ = GetShaderLocation(s, "lightDir");
  locAmbient_ = GetShaderLocation(s, "ambient");

  Upload();
}

void RetroShader::SetParams(const RetroShaderParams& params) {
  params_ = params;
  Upload();
}

void RetroShader::Upload() const {
  const Shader& s = shader_->Get();
  SetShaderValue(s, locSnapResolution_, &params_.snapResolution, SHADER_UNIFORM_VEC2);
  SetShaderValue(s, locColorLevels_, &params_.colorLevels, SHADER_UNIFORM_FLOAT);
  SetShaderValue(s, locDither_, &params_.ditherStrength, SHADER_UNIFORM_FLOAT);
  SetShaderValue(s, locLightDir_, &params_.lightDirection, SHADER_UNIFORM_VEC3);
  SetShaderValue(s, locAmbient_, &params_.ambient, SHADER_UNIFORM_FLOAT);
}

} // namespace retro