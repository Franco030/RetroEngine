#include "core/RetroEngine.hpp"

#include <algorithm>
#include <cmath>
#include <stdexcept>
#include <utility>

namespace retro {

// ---------------------------------------------------------------------------
// WindowGuard
// ---------------------------------------------------------------------------
RetroEngine::WindowGuard::WindowGuard(const EngineConfig& config) {
  unsigned int flags = 0;
  if (config.vsync)
    flags |= FLAG_VSYNC_HINT;
  if (config.resizable)
    flags |= FLAG_WINDOW_RESIZABLE;
  SetConfigFlags(flags);

  InitWindow(config.windowWidth, config.windowHeight, config.title.c_str());
  if (!IsWindowReady()) {
    CloseWindow();
    throw std::runtime_error("No se pudo inicializar la ventana / contexto OpenGl");
  }

  SetTargetFPS(config.targetFps);
  SetExitKey(KEY_NULL);
}

RetroEngine::WindowGuard::~WindowGuard() { CloseWindow(); }

// ---------------------------------------------------------------------------
// RetroEngine
// ---------------------------------------------------------------------------

namespace {

RetroShaderParams DefaultShaderParams(const EngineConfig& c) {
  RetroShaderParams p;
  p.snapResolution = {static_cast<float>(c.internalWidth), static_cast<float>(c.internalHeight)};
  p.fogColor = {c.clearColor.r / 255.0f, c.clearColor.g / 255.0f, c.clearColor.b / 255.0f};
  return p;
}

} // namespace

RetroEngine::RetroEngine(EngineConfig config)
    : config_(std::move(config)), window_(config_),
      target_(config_.internalWidth, config_.internalHeight),
      assets_(std::string(GetApplicationDirectory()) + "assets/"),
      retroShader_(assets_.GetShader("shaders/retro.vs", "shaders/retro.fs"),
                   assets_.GetShader("shaders/retro.vs", "shaders/retro_unlit.fs"),
                   DefaultShaderParams(config_)) {}

void RetroEngine::Run(const UpdateFn& update, const DrawFn& draw) {
  bool running = true;

  while (running && !WindowShouldClose()) {
    if (IsKeyPressed(KEY_ESCAPE))
      running = false;
    if (IsKeyPressed(KEY_F11))
      ToggleBorderlessWindowed();

    if (update)
      update(GetFrameTime());

    BeginTextureMode(target_.Get());
    ClearBackground(config_.clearColor);
    if (draw)
      draw();
    EndTextureMode();

    BeginDrawing();
    ClearBackground(BLACK);
    Present();
    EndDrawing();
  }
}

Rectangle RetroEngine::PresentationRect() const {
  const float winW = static_cast<float>(GetScreenWidth());
  const float winH = static_cast<float>(GetScreenHeight());
  const float intW = static_cast<float>(config_.internalWidth);
  const float intH = static_cast<float>(config_.internalHeight);

  float scale = std::min(winW / intW, winH / intH);
  if (config_.integerScaling && scale >= 1.0f)
    scale = std::floor(scale);

  const float w = intW * scale;
  const float h = intH * scale;

  return {std::floor((winW - w) * 0.5f), std::floor((winH - h) * 0.5f), w, h};
}

void RetroEngine::SetClearColor(Color color) {
  config_.clearColor = color;
  RetroShaderParams p = retroShader_.Params();
  p.fogColor = {color.r / 255.0f, color.g / 255.0f, color.b / 255.0f};
  retroShader_.SetParams(p);
}

Vector2 RetroEngine::WindowToInternal(Vector2 windowPos) const {
  const Rectangle dst = PresentationRect();
  return {(windowPos.x - dst.x) * static_cast<float>(config_.internalWidth) / dst.width,
          (windowPos.y - dst.y) * static_cast<float>(config_.internalHeight) / dst.height};
}

void RetroEngine::Present() const {
  const Texture2D& tex = target_.Get().texture;

  const Rectangle src = {0.0f, 0.0f, static_cast<float>(tex.width),
                         -static_cast<float>(tex.height)};

  DrawTexturePro(tex, src, PresentationRect(), {0.0f, 0.0f}, 0.0f, WHITE);
}

} // namespace retro