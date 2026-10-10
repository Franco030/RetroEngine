#pragma once

#include "assets/AssetManager.hpp"
#include "core/RenderTarget.hpp"
#include "graphics/PostProcess.hpp"
#include "graphics/RetroShader.hpp"
#include "ui/Ui.hpp"

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
#define RETRO_WINDOW_HEIGHT 960
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
  Color clearColor = {24, 20, 32, 255};
  std::string uiFont = "fonts/ui.ttf";
};

class RetroEngine {
public:
  using UpdateFn = std::function<void(float dt)>;
  using DrawFn = std::function<void()>;
  using OverlayFn = std::function<void(Ui&)>;

  explicit RetroEngine(EngineConfig config = {});
  ~RetroEngine() = default;

  RetroEngine(const RetroEngine&) = delete;
  RetroEngine& operator=(const RetroEngine&) = delete;
  RetroEngine(RetroEngine&&) = delete;
  RetroEngine& operator=(RetroEngine&&) = delete;

  void Run(const UpdateFn& update, const DrawFn& draw, const OverlayFn& overlay = {});

  AssetManager& Assets() noexcept { return assets_; }
  RetroShader& Retro() noexcept { return retroShader_; }
  PostProcess& Post() noexcept { return post_; }
  const EngineConfig& Config() const noexcept { return config_; }

  void SetClearColor(Color color);

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

  // orden: se destruyen en orden inverso.
  //   post_ -> ui_ -> retroShader_ -> assets_ -> target_ -> window_ (CloseWindow)
  // Todo Unload* necesita el contexto OpenGL vivo.
  EngineConfig config_;
  WindowGuard window_;
  RenderTarget target_;
  AssetManager assets_;
  RetroShader retroShader_;
  Ui ui_;
  PostProcess post_;
};

} // namespace retro