#include "core/RenderTarget.hpp"

#include <stdexcept>
#include <string>
#include <utility>

namespace retro {

RenderTarget::RenderTarget(int width, int height) : target_(::LoadRenderTexture(width, height)) {
  if (!IsRenderTextureValid(target_)) {
    throw std::runtime_error("No se pudo crear el RenderTexture2D de " + std::to_string(width) +
                             "x" + std::to_string(height));
  }

  SetTextureFilter(target_.texture, TEXTURE_FILTER_BILINEAR);
  SetTextureWrap(target_.texture, TEXTURE_WRAP_CLAMP);
}

RenderTarget::~RenderTarget() { Release(); }

RenderTarget::RenderTarget(RenderTarget&& other) noexcept
    : target_(std::exchange(other.target_, RenderTexture2D{})) {}

RenderTarget& RenderTarget::operator=(RenderTarget&& other) noexcept {
  if (this != &other) {
    Release();
    target_ = std::exchange(other.target_, RenderTexture2D{});
  }

  return *this;
}

void RenderTarget::Release() noexcept {
  if (target_.id != 0) {
    UnloadRenderTexture(target_);
    target_ = RenderTexture2D{};
  }
}

} // namespace retro