#include "graphics/RetroShader.hpp"

#include <algorithm>
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

  const int on = 1;
  SetUniform(lit_.shader->Get(), lit_.loc.useLighting, &on, SHADER_UNIFORM_INT);
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
  l.useLighting = GetShaderLocation(s, "useLighting");
  l.lightCount = GetShaderLocation(s, "lightCount");
  l.lightPos = GetShaderLocation(s, "lightPos[0]");
  l.lightColor = GetShaderLocation(s, "lightColor[0]");

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

void RetroShader::SetPointLights(std::span<const PointLight> lights) {
  const int n = static_cast<int>(std::min<std::size_t>(lights.size(), kMaxPointLights));

  float pos[kMaxPointLights * 4] = {};
  float col[kMaxPointLights * 3] = {};
  for (int i = 0; i < n; ++i) {
    pos[i * 4 + 0] = lights[i].position.x;
    pos[i * 4 + 1] = lights[i].position.y;
    pos[i * 4 + 2] = lights[i].position.z;
    pos[i * 4 + 3] = lights[i].radius;
    col[i * 3 + 0] = lights[i].color.x;
    col[i * 3 + 1] = lights[i].color.y;
    col[i * 3 + 2] = lights[i].color.z;
  }

  const Shader& s = lit_.shader->Get();
  const Locations& l = lit_.loc;
  SetUniform(s, l.lightCount, &n, SHADER_UNIFORM_INT);
  if (n > 0 && l.lightPos >= 0 && l.lightColor >= 0) {
    SetShaderValueV(s, l.lightPos, pos, SHADER_UNIFORM_VEC4, n);
    SetShaderValueV(s, l.lightColor, col, SHADER_UNIFORM_VEC3, n);
  }
}

} // namespace retro