#include <raylib.h>

#include "assets/AssetManager.hpp"

int main() {
  InitWindow(RETRO_WINDOW_WIDTH, RETRO_WINDOW_HEIGHT, "RetroEngine - Hito 2");
  SetTargetFPS(60);

  {
    retro::AssetManager assets(std::string(GetApplicationDirectory()) + "assets/");
    auto tex = assets.GetTexture("textures/texture.png");

    while (!WindowShouldClose()) {
      BeginDrawing();
      ClearBackground(DARKGRAY);
      DrawTextureEx(tex->Get(), {20, 20}, 0.0f, 8.0f, WHITE);
      EndDrawing();
    }
  }

  CloseWindow();
  return 0;
}