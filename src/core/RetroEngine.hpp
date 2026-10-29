#pragma once

#include "assets/AssetManager.hpp"
#include "audio/AudioSystem.hpp"
#include "core/RenderTarget.hpp"
#include "graphics/PostProcess.hpp"
#include "graphics/RetroShader.hpp"
#include "ui/Ui.hpp"

#include <raylib.h>

#include <functional>
#include <optional>
#include <string>
#include <utility>

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
  std::string uiFont = "fonts/dpcomic.ttf";
  std::string assetRoot;
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

  // El bucle dura hasta que se cierra la ventana o se llama a Quit (Esc lo decide el juego).
  void Run(const UpdateFn& update, const DrawFn& draw, const OverlayFn& overlay = {});
  void Quit() noexcept { quit_ = true; }

  // Foto: guarda en 'file' (PNG) la imagen tal como se ve (con el post-proceso, a la resolucion
  // de la ventana y sin las barras negras) al final del fotograma en curso. Va por el propio
  // motor porque a pantalla completa muchas capturas del sistema salen negras.
  void RequestScreenshot(std::string file) { shotFile_ = std::move(file); }
  // El resultado de la ultima foto pedida (una vez): true si se guardo.
  std::optional<bool> TakeScreenshotResult() noexcept {
    return std::exchange(shotResult_, std::nullopt);
  }

  AssetManager& Assets() noexcept { return assets_; }
  RetroShader& Retro() noexcept { return retroShader_; }
  PostProcess& Post() noexcept { return post_; }
  const EngineConfig& Config() const noexcept { return config_; }

  AudioSystem& Audio() noexcept { return audio_; }

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

  class AudioGuard {
  public:
    AudioGuard();
    ~AudioGuard();
    AudioGuard(const AudioGuard&) = delete;
    AudioGuard& operator=(const AudioGuard&) = delete;
    bool Ready() const noexcept { return ready_; }

  private:
    bool ready_ = false;
  };

  void Present() const;

  // orden: se destruyen en orden inverso.
  //   audio_ -> post_ -> ui_ -> retroShader_ -> assets_ -> target_ -> audioGuard_ -> window_
  EngineConfig config_;
  WindowGuard window_;
  AudioGuard audioGuard_;
  RenderTarget target_;
  AssetManager assets_;
  RetroShader retroShader_;
  Ui ui_;
  PostProcess post_;
  AudioSystem audio_;
  bool quit_ = false;
  std::string shotFile_;            // foto pedida para este fotograma (vacio: ninguna)
  std::optional<bool> shotResult_;  // como salio la ultima

  bool SaveScreenshot(const std::string& file) const;
};

} // namespace retro