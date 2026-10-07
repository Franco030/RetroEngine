#pragma once

#include "assets/AssetManager.hpp"
#include "core/RenderTarget.hpp"
#include "graphics/RetroShader.hpp"

#include <raylib.h>

#include <functional>
#include <string>

#ifndef RETRO_INTERNAL_WIDTH
#define RETRO_INTERNAL_WIDTH 320
#endif
#ifndef RETRO_INTERNAL_HEIGHT
#define RETRO_INTERNAL_HEIGHT 240
#endif
#ifndef RETRO_WINDOW_WIDTH
#define RETRO_WINDOW_WIDTH 1280
#endif
#ifndef RETRO_WINDOW_HEIGHT
#define RETRORETRO_WINDOW_HEIGHT 960
#endif

namespace retro {

struct EngineConfig {
  std::string title = "RetroEngine";
  int internalWidth = RETRO_INTERNAL_WIDTH;
  int internalHeight = RETRO_INTERNAL_HEIGHT;
  int windowWidth = RETRO_WINDOW_WIDTH;
  int windowHeight = RETRO_WINDOW_HEIGHT;
  int targetFps = 60;
  bool vsync = true;
  bool resizable = true;
  bool integerScaling = false;
  Color clearColor = {25, 20, 32, 255};
};

class RetroEngine {
public:
  using UpdateFn = std::function<void(float dt)>;
  using DrawFn = std::function<void()>;

  explicit RetroEngine(EngineConfig config = {});
  ~RetroEngine() = default;

  RetroEngine(const RetroEngine&) = delete;
  RetroEngine& operator=(const RetroEngine&) = delete;
  RetroEngine(RetroEngine&&) = delete;
  RetroEngine& operator=(RetroEngine&&) = delete;

  RetroShader& Retro() noexcept { return retroShader_; }

  void Run(const UpdateFn& update, const DrawFn& draw);

  AssetManager& Assets() noexcept { return assets_; }
  const EngineConfig& Config() const noexcept { return config_; }

  Rectangle PresentationRect() const;

  Vector2 WindowToInternal(Vector2 windowPos) const;

private:
  class WindowGuard {
  public:
    explicit WindowGuard(const EngineConfig& config);
    ~WindowGuard();
    WindowGuard(const WindowGuard&) = delete;
    WindowGuard& operator=(const WindowGuard&) = delete;
  };

  void Present() const;

  EngineConfig config_;
  WindowGuard window_;
  RenderTarget target_;
  AssetManager assets_;
  RetroShader retroShader_;
};

} // namespace retro