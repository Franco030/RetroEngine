#include "graphics/RetroShader.hpp"

#include <stdexcept>
#include <utility>

namespace retro {

namespace {

void SetUniform(const Shader& s, int loc, const void* value, int type) {
  if (loc >= 0)
    SetShaderValue(s, loc, value, type);
}

} // namespace

RetroShader::RetroShader(std::shared_ptr<ShaderResource> lit, std::shared_ptr<ShaderResource> unlit,
                         std::shared_ptr<ShaderResource> sprite, const RetroShaderParams& params)
    : lit_(MakeProgram(std::move(lit))), unlit_(MakeProgram(std::move(unlit))),
      sprite_(MakeProgram(std::move(sprite))), params_(params) {
  Upload(lit_);
  Upload(unlit_);
  Upload(sprite_);
}

RetroShader::Program RetroShader::MakeProgram(std::shared_ptr<ShaderResource> shader) {
  if (!shader || !shader->IsValid()) {
    throw std::invalid_argument("RetroShader: shader nulo o invalido");
  }

  const Shader& s = shader->Get();
  Locations l;
  l.snapResolution = GetShaderLocation(s, "snapResolution");
  l.colorLevels = GetShaderLocation(s, "colorLevels");
  l.dither = GetShaderLocation(s, "ditherStrength");
  l.affine = GetShaderLocation(s, "affineAmount");
  l.lightDir = GetShaderLocation(s, "lightDir");
  l.ambient = GetShaderLocation(s, "ambient");
  l.fogColor = GetShaderLocation(s, "fogColor");
  l.fogStart = GetShaderLocation(s, "fogStart");
  l.fogEnd = GetShaderLocation(s, "fogEnd");

  return Program{std::move(shader), l};
}

void RetroShader::SetParams(const RetroShaderParams& params) {
  params_ = params;
  Upload(lit_);
  Upload(unlit_);
  Upload(sprite_);
}

void RetroShader::Upload(const Program& p) const {
  const Shader& s = p.shader->Get();
  const Locations& l = p.loc;

  const float fogStart = params_.fogEnabled ? params_.fogStart : 1.0e6f;
  const float fogEnd = params_.fogEnabled ? params_.fogEnd : 2.0e6f;

  SetUniform(s, l.snapResolution, &params_.snapResolution, SHADER_UNIFORM_VEC2);
  SetUniform(s, l.colorLevels, &params_.colorLevels, SHADER_UNIFORM_FLOAT);
  SetUniform(s, l.dither, &params_.ditherStrength, SHADER_UNIFORM_FLOAT);
  SetUniform(s, l.affine, &params_.affineAmount, SHADER_UNIFORM_FLOAT);
  SetUniform(s, l.lightDir, &params_.lightDirection, SHADER_UNIFORM_VEC3);
  SetUniform(s, l.ambient, &params_.ambient, SHADER_UNIFORM_FLOAT);
  SetUniform(s, l.fogColor, &params_.fogColor, SHADER_UNIFORM_VEC3);
  SetUniform(s, l.fogStart, &fogStart, SHADER_UNIFORM_FLOAT);
  SetUniform(s, l.fogEnd, &fogEnd, SHADER_UNIFORM_FLOAT);
}

} // namespace retro