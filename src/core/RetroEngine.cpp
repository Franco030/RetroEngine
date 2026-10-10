#include "core/RetroEngine.hpp"

#include <algorithm>
#include <cmath>
#include <iostream>
#include <memory>
#include <stdexcept>
#include <utility>

namespace retro {

namespace {

RetroShaderParams DefaultShaderParams(const EngineConfig& c) {
  RetroShaderParams p;
  p.snapResolution = {static_cast<float>(c.internalWidth), static_cast<float>(c.internalHeight)};
  p.fogColor = {c.clearColor.r / 255.0f, c.clearColor.g / 255.0f, c.clearColor.b / 255.0f};
  return p;
}

std::shared_ptr<FontResource> LoadUiFont(const EngineConfig& c, AssetManager& assets) {
  if (c.uiFont.empty())
    return nullptr;
  if (!FileExists((assets.Root() + c.uiFont).c_str())) {
    std::cerr << "Aviso: no existe la fuente 'assets/" << c.uiFont
              << "'; se usa la fuente por defecto de Raylib\n";
    return nullptr;
  }
  return assets.GetFont(c.uiFont, 48);
}

} // namespace

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
    throw std::runtime_error("No se pudo inicializar la ventana / contexto OpenGL");
  }

  SetTargetFPS(config.targetFps);
  SetExitKey(KEY_NULL);
}

RetroEngine::WindowGuard::~WindowGuard() { CloseWindow(); }

// ---------------------------------------------------------------------------
// RetroEngine
// ---------------------------------------------------------------------------
RetroEngine::RetroEngine(EngineConfig config)
    : config_(std::move(config)), window_(config_),
      target_(config_.internalWidth, config_.internalHeight),
      assets_(std::string(GetApplicationDirectory()) + "assets/"),
      retroShader_(assets_.GetShader("shaders/retro.vs", "shaders/retro.fs"),
                   assets_.GetShader("shaders/retro.vs", "shaders/retro_unlit.fs"),
                   assets_.GetShader("shaders/retro.vs", "shaders/retro_sprite.fs"),
                   DefaultShaderParams(config_)),
      ui_(LoadUiFont(config_, assets_)), post_(assets_.GetShader("", "shaders/post.fs")) {}

void RetroEngine::Run(const UpdateFn& update, const DrawFn& draw, const OverlayFn& overlay) {
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
    ClearBackground(BLACK); // barras del letterbox
    Present();
    if (overlay) {
      ui_.Begin(PresentationRect());
      overlay(ui_);
    }
    EndDrawing();
  }
}

void RetroEngine::SetClearColor(Color color) {
  config_.clearColor = color;
  RetroShaderParams p = retroShader_.Params();
  p.fogColor = {color.r / 255.0f, color.g / 255.0f, color.b / 255.0f};
  retroShader_.SetParams(p);
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

Vector2 RetroEngine::WindowToInternal(Vector2 windowPos) const {
  const Rectangle dst = PresentationRect();
  return {(windowPos.x - dst.x) * static_cast<float>(config_.internalWidth) / dst.width,
          (windowPos.y - dst.y) * static_cast<float>(config_.internalHeight) / dst.height};
}

void RetroEngine::Present() const { post_.Draw(target_.Get().texture, PresentationRect()); }

} // namespace retro