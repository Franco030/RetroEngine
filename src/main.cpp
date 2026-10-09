#include <raylib.h>
#include <raymath.h>

#include <algorithm>
#include <cstdint>
#include <exception>
#include <iostream>
#include <string>
#include <vector>

#include "core/RetroEngine.hpp"
#include "game/GameState.hpp"
#include "graphics/DebugDraw.hpp"
#include "graphics/RetroCamera.hpp"
#include "graphics/ShaderScope.hpp"
#include "player/Player.hpp"
#include "world/ActionRunner.hpp"
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

static void DrawMessageBox(const std::string& text, int screenW, int screenH) {
  std::vector<std::string> lines;
  std::size_t start = 0;
  while (true) {
    const std::size_t nl = text.find('\n', start);
    lines.push_back(text.substr(start, nl == std::string::npos ? nl : nl - start));
    if (nl == std::string::npos)
      break;
    start = nl + 1;
  }

  constexpr int kFont = 10, kLineH = 12;
  int maxW = 0;
  for (const std::string& l : lines)
    maxW = std::max(maxW, MeasureText(l.c_str(), kFont));

  const int boxW = maxW + 12;
  const int boxH = static_cast<int>(lines.size()) * kLineH + 8;
  const int x = (screenW - boxW) / 2;
  const int y = screenH - boxH - 20;

  DrawRectangle(x, y, boxW, boxH, {0, 0, 0, 200});
  DrawRectangleLines(x, y, boxW, boxH, RAYWHITE);
  for (std::size_t i = 0; i < lines.size(); ++i) {
    DrawText(lines[i].c_str(), x + 6, y + 4 + static_cast<int>(i) * kLineH, kFont, RAYWHITE);
  }
}

int main() {
  try {
    retro::EngineConfig config;
    config.title = "RetroEngine - Hito 12";
    retro::RetroEngine engine(config);
    retro::RetroCamera camera;

    retro::SceneManager scenes(engine, camera, {"scenes/demo.json"});
    scenes.LoadNow(0);

    retro::Player player;
    retro::GameState state;
    retro::ActionRunner runner(scenes, player, state);

    std::vector<retro::WorldBox> colliders;
    std::vector<retro::Action> firedActions;
    std::uint64_t seenGeneration = scenes.Generation();
    bool firstPerson = true;
    bool showColliders = false;
    std::string interactPrompt;

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

          if (IsKeyPressed(KEY_F6)) {
            state.Clear();
            std::cout << "Estado de la partida reiniciado (F5 para recargar la escena)\n";
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
          state.Update(dt);

          if (scenes.Generation() != seenGeneration) {
            seenGeneration = scenes.Generation();
            spawnPlayer();
          }

          if (retro::Scene* s = scenes.CurrentMut())
            s->RefreshVisibility(state);

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

          // --- Triggers ---
          if (firstPerson && !scenes.Transitioning()) {
            if (retro::Scene* s = scenes.CurrentMut()) {
              const retro::PlayerSettings& cfg = player.Settings();
              firedActions.clear();
              s->UpdateTriggers(player.Feet(), cfg.radius, cfg.height, state, firedActions);
              runner.Run(firedActions);
            }
          }

          // --- Interaccion (E) ---
          interactPrompt.clear();
          if (firstPerson && !scenes.Transitioning()) {
            if (retro::Scene* s = scenes.CurrentMut()) {
              const Vector3 origin = camera.Position();
              const Vector3 dir = Vector3Normalize(Vector3Subtract(camera.Target(), origin));
              const auto hit = s->FindInteractable(origin, dir, state);
              if (hit.entity) {
                interactPrompt = "E: " + hit.entity->interact->prompt;
                if (IsKeyPressed(KEY_E)) {
                  firedActions.clear();
                  s->Interact(*hit.entity, state, firedActions);
                  runner.Run(firedActions);
                }
              }
            }
          }

          if (retro::Scene* s = scenes.CurrentMut())
            s->RefreshVisibility(state);
          HandleDebugKeys(engine);
        },
        [&]() {
          camera.Begin();
          scenes.Draw();

          if (const retro::Scene* s = scenes.Current()) {
            retro::ShaderScope lines(engine.Retro().Unlit()->Get());
            for (const retro::Trigger& t : s->triggers) {
              if (t.marker)
                retro::DrawPortalFrame(t.box, t.color);
              if (showColliders)
                retro::DrawWorldBoxWires(t.box, {80, 255, 120, 255});
            }
          }

          if (showColliders) {
            for (const retro::WorldBox& b : colliders) {
              retro::DrawWorldBoxWires(b, {255, 80, 80, 255});
            }
          }
          camera.End();

          scenes.DrawFade();

          const int w = engine.Config().internalWidth;
          const int h = engine.Config().internalHeight;
          const Color crossColor = interactPrompt.empty() ? RAYWHITE : YELLOW;
          if (firstPerson)
            DrawRectangle(w / 2 - 1, h / 2 - 1, 2, 2, crossColor);

          if (!interactPrompt.empty()) {
            const int tw = MeasureText(interactPrompt.c_str(), 10);
            DrawText(interactPrompt.c_str(), w / 2 - tw / 2, h / 2 + 10, 10, YELLOW);
          }

          DrawText("1:jitter 2:color 3:dither 4:niebla  F5:recargar F6:reset", 4, 4, 10, RAYWHITE);
          DrawText(firstPerson ? "WASD mover  Shift correr  Espacio saltar"
                               : "Arrastrar/flechas: orbitar  Rueda: zoom",
                   4, 16, 10, RAYWHITE);
          DrawText("Tab: escena  F2: camara libre  F3: colisiones/triggers", 4, 28, 10, RAYWHITE);
          DrawText(TextFormat("Escena %d/%d: %s", static_cast<int>(scenes.Index()) + 1,
                              static_cast<int>(scenes.Count()), scenes.CurrentPath().c_str()),
                   4, h - 12, 10, RAYWHITE);

          if (state.HasMessage())
            DrawMessageBox(state.MessageText(), w, h);
        });
  } catch (const std::exception& e) {
    std::cerr << "Error fatal: " << e.what() << '\n';
    return 1;
  }
  return 0;
}