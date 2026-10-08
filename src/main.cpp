#include <raylib.h>

#include <cstdint>
#include <exception>
#include <iostream>
#include <vector>

#include "core/RetroEngine.hpp"
#include "graphics/DebugDraw.hpp"
#include "graphics/RetroCamera.hpp"
#include "player/Player.hpp"
#include "world/SceneManager.hpp"

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
  if (IsKeyPressed(KEY_FOUR)) {
    p.fogEnabled = !p.fogEnabled;
    changed = true;
  }
  if (changed)
    engine.Retro().SetParams(p);
}

int main() {
  try {
    retro::EngineConfig config;
    config.title = "RetroEngine - Hito 10";
    retro::RetroEngine engine(config); // primero: muere al final
    retro::RetroCamera camera;

    retro::SceneManager scenes(engine, camera,
                               {"scenes/demo.json", "scenes/noche.json", "scenes/aldea.json"});
    scenes.LoadNow(0);

    // Despues del motor: su destructor devuelve el cursor y necesita la ventana.
    retro::Player player;
    std::vector<retro::WorldBox> colliders;
    std::uint64_t seenGeneration = scenes.Generation();
    bool firstPerson = true;
    bool showColliders = false;

    auto spawnPlayer = [&]() {
      if (const retro::Scene* s = scenes.Current()) {
        player.Spawn(s->spawn.position, s->spawn.yaw);
      }
    };
    spawnPlayer();
    player.SetActive(true);

    engine.Run(
        [&](float dt) {
          if (IsKeyPressed(KEY_TAB)) {
            if (IsKeyDown(KEY_LEFT_SHIFT) || IsKeyDown(KEY_RIGHT_SHIFT)) {
              scenes.Previous();
            } else {
              scenes.Next();
            }
          }

          if (IsKeyPressed(KEY_F5)) {
            try {
              scenes.Reload();
              std::cout << "Escena recargada: " << scenes.CurrentPath() << '\n';
            } catch (const std::exception& e) {
              std::cerr << "Error al recargar: " << e.what() << '\n';
            }
          }

          if (IsKeyPressed(KEY_F2)) {
            firstPerson = !firstPerson;
            player.SetActive(firstPerson);
            if (!firstPerson) {
              const Vector3 f = player.Feet();
              camera.OrbitAround({f.x, f.y + 1.0f, f.z}, 8.0f);
            }
          }
          if (IsKeyPressed(KEY_F3))
            showColliders = !showColliders;

          scenes.Update(dt);

          // Escena nueva (cambio o F5): el jugador reaparece en su spawn.
          if (scenes.Generation() != seenGeneration) {
            seenGeneration = scenes.Generation();
            spawnPlayer();
          }

          colliders.clear();
          if (const retro::Scene* s = scenes.Current())
            s->CollectColliders(colliders);

          if (firstPerson) {
            if (!scenes.Transitioning())
              player.Update(dt, colliders);
            player.ApplyToCamera(camera);
          } else {
            camera.UpdateOrbit(dt);
          }

          HandleDebugKeys(engine);
        },
        [&]() {
          camera.Begin();
          scenes.Draw();
          if (showColliders) {
            for (const retro::WorldBox& b : colliders) {
              retro::DrawWorldBoxWires(b, {255, 80, 80, 255});
            }
          }
          camera.End();

          scenes.DrawFade();

          const int cx = engine.Config().internalWidth / 2;
          const int cy = engine.Config().internalHeight / 2;
          if (firstPerson)
            DrawRectangle(cx - 1, cy - 1, 2, 2, RAYWHITE); // mira

          DrawText("1:jitter 2:color 3:dither 4:niebla  F5:recargar", 4, 4, 10, RAYWHITE);
          DrawText(firstPerson ? "WASD mover  Shift correr  Espacio saltar"
                               : "Arrastrar/flechas: orbitar  Rueda: zoom",
                   4, 16, 10, RAYWHITE);
          DrawText("Tab: escena  F2: camara libre  F3: colisiones", 4, 28, 10, RAYWHITE);
          DrawText(TextFormat("Escena %d/%d: %s", static_cast<int>(scenes.Index()) + 1,
                              static_cast<int>(scenes.Count()), scenes.CurrentPath().c_str()),
                   4, 228, 10, RAYWHITE);
        });
  } catch (const std::exception& e) {
    std::cerr << "Error fatal: " << e.what() << '\n';
    return 1;
  }
  return 0;
}