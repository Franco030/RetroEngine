#include <raylib.h>

#include <exception>
#include <iostream>
#include <memory>

#include "assets/Resources.hpp"
#include "core/RetroEngine.hpp"
#include "graphics/DebugDraw.hpp"
#include "graphics/RetroCamera.hpp"
#include "world/Entity.hpp"
#include "world/Scene.hpp"

static void HandleDebugKeys(retro::RetroEngine& engine) {
  static bool jitter = true, lowColor = true, dither = true;

  retro::RetroShaderParams p = engine.Retro().Params();
  bool changed = false;

  if (IsKeyPressed(KEY_ONE)) {
    jitter = !jitter;
    p.snapResolution = jitter ? Vector2{320.0f, 240.0f} : Vector2{4096.0f, 4096.0f};
    changed = true;
  }
  if (IsKeyPressed(KEY_TWO)) {
    lowColor = !lowColor;
    p.colorLevels = lowColor ? 32.0f : 256.0f;
    changed = true;
  }
  if (IsKeyPressed(KEY_THREE)) {
    dither = !dither;
    p.ditherStrength = dither ? 1.0f : 0.0f;
    changed = true;
  }
  if (changed)
    engine.Retro().SetParams(p);
}

int main() {
  try {
    retro::EngineConfig config;
    config.title = "RetroEngine - Hito 5";
    retro::RetroEngine engine(config);

    auto house = engine.Assets().GetModel("models/casa.obj");
    auto texture = engine.Assets().GetTexture("textures/test.png");
    house->SetShader(engine.Retro().Resource());

    retro::Scene scene;

    auto& center = scene.Add(std::make_unique<retro::Entity>("casa_centro", house, texture));
    center.SetUpdate([](retro::Entity& e, float dt) { e.transform.rotation.y += 20.0f * dt; });

    auto& left = scene.Add(std::make_unique<retro::Entity>("casa_izq", house, texture));
    left.transform.position = {-4.0f, 0.0f, -1.0f};
    left.transform.scale = {0.7f, 0.7f, 0.7f};
    left.transform.rotation = {0.0f, 30.0f, 0.0f};

    auto& right = scene.Add(std::make_unique<retro::Entity>("casa_der", house, texture));
    right.transform.position = {4.0f, 0.0f, 1.0f};
    right.transform.scale = {1.0f, 1.6f, 1.0f};
    right.tint = {255, 200, 200, 255};
    right.SetUpdate([](retro::Entity& e, float dt) { e.transform.rotation.y -= 35.0f * dt; });

    retro::RetroCamera camera;
    camera.SetPosition({7.0f, 5.0f, 9.0f});
    camera.LookAt({0.0f, 1.0f, 0.0f});
    camera.SetFov(60.0f);

    engine.Run(
        [&](float dt) {
          camera.UpdateOrbit(dt);
          scene.Update(dt);
          HandleDebugKeys(engine);
        },
        [&]() {
          camera.Begin();
          retro::DrawFloorGrid(10, 1.0f, {70, 60, 100, 255}, {140, 90, 200, 255});
          scene.Draw();
          camera.End();

          DrawText("1:jitter 2:color 3:dither", 4, 4, 10, RAYWHITE);
          DrawText("Arrastrar/flechas: orbitar  Rueda: zoom", 4, 16, 10, RAYWHITE);
        });
  } catch (const std::exception& e) {
    std::cerr << "Error fatal: " << e.what() << '\n';
    return 1;
  }
  return 0;
}