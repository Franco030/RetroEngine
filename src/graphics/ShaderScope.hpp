#pragma once

#include <raylib.h>

namespace retro {

class ShaderScope {
public:
  explicit ShaderScope(const Shader& shader) { BeginShaderMode(shader); }
  ~ShaderScope() { EndShaderMode(); }

  ShaderScope(const ShaderScope&) = delete;
  ShaderScope& operator=(const ShaderScope&) = delete;
};

} // namespace retro