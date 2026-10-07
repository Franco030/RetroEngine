#include <raylib.h>

#include <exception>
#include <iostream>
#include <memory>

#include "assets/Resources.hpp"
#include "core/RetroEngine.hpp"

static void DrawFloorGrid(int halfCells, float spacing, Color line, Color axis) {
  const float extent = halfCells * spacing;
  for (int i = -halfCells; i <= halfCells; ++i) {
    const Color c = (i == 0) ? axis : line;
    const float p = i * spacing;
    DrawLine3D({p, 0.0f, -extent}, {p, 0.0f, extent}, c);
    DrawLine3D({-extent, 0.0f, p}, {extent, 0.0f, p}, c);
  }
}

int main() {
  try {
    retro::EngineConfig config;
    config.title = "RetroEngine - Hito 4";
    retro::RetroEngine engine(config);

    auto shader = engine.Retro().Resource();
    auto texture = engine.Assets().GetTexture("textures/test.png");

    auto house = engine.Assets().GetModel("models/casa.obj");
    house->SetShader(shader);
    SetMaterialTexture(&house->Get().materials[0], MATERIAL_MAP_DIFFUSE, texture->Get());

    Camera3D camera{};
    camera.position = {5.0f, 4.0f, 5.0f};
    camera.target = {0.0f, 0.5f, 0.0f};
    camera.up = {0.0f, 1.0f, 0.0f};
    camera.fovy = 60.0f;
    camera.projection = CAMERA_PERSPECTIVE;

    float angle = 0.0f;
    bool jitter = true;
    bool low = true;
    bool dither = true;

    engine.Run(
        [&](float dt) {
          angle += 20.0f * dt;

          retro::RetroShaderParams p = engine.Retro().Params();
          bool changed = false;
          if (IsKeyPressed(KEY_ONE)) {
            jitter = !jitter;
            p.snapResolution = jitter ? Vector2{320.0f, 240.0f} : Vector2{4096.0f, 4096.0f};
            changed = true;
          }
          if (IsKeyPressed(KEY_TWO)) {
            low = !low;
            p.colorLevels = low ? 32.0f : 256.0f;
            changed = true;
          }
          if (IsKeyPressed(KEY_THREE)) {
            dither = !dither;
            p.ditherStrength = dither ? 1.0f : 0.0f;
            changed = true;
          }
          if (changed)
            engine.Retro().SetParams(p);
        },
        [&]() {
          BeginMode3D(camera);
          DrawFloorGrid(10, 1.0f, {70, 60, 100, 255}, {140, 90, 200, 255});
          DrawModelEx(house->Get(), {0.0f, 0.75f, 0.0f}, {0.0f, 1.0f, 0.0f}, angle,
                      {1.5f, 1.5f, 1.5f}, WHITE);
          EndMode3D();

          DrawText("1:jitter 2:color 3:dither", 4, 4, 10, RAYWHITE);
        });
  } catch (const std::exception& e) {
    std::cerr << "Error fatal: " << e.what() << '\n';
    return 1;
  }
  return 0;
}