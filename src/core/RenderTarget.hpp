#pragma once

#include <raylib.h>

namespace retro {

class RenderTarget {
public:
  RenderTarget(int width, int height);
  ~RenderTarget();

  RenderTarget(const RenderTarget&) = delete;
  RenderTarget& operator=(const RenderTarget&) = delete;
  RenderTarget(RenderTarget&& other) noexcept;
  RenderTarget& operator=(RenderTarget&& other) noexcept;

  const RenderTexture2D& Get() const noexcept { return target_; }
  int Width() const noexcept { return target_.texture.width; }
  int Height() const noexcept { return target_.texture.height; }

private:
  void Release() noexcept;
  RenderTexture2D target_{};
};

} // namespace retro