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
#include "ui/Ui.hpp"
#include "world/ActionRunner.hpp"
#include "world/SceneManager.hpp"

static void HandleDebugKeys(retro::RetroEngine& engine) {
  static bool jitter = true, lowColor = true, dither = true;
  static int affineStep = 0;

  retro::RetroShaderParams p = engine.Retro().Params();
  bool changed = false;

  if (IsKeyPressed(KEY_ONE)) {
    jitter = !jitter;
    p.snapResolution = jitter ? Vector2{static_cast<float>(engine.Config().internalWidth),
                                        static_cast<float>(engine.Config().internalHeight)}
                              : Vector2{4096.0f, 4096.0f};
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
  if (IsKeyPressed(KEY_FIVE)) {
    static constexpr float kLevels[3] = {0.35f, 1.0f, 0.0f};
    affineStep = (affineStep + 1) % 3;
    p.affineAmount = kLevels[affineStep];
    changed = true;
  }
  if (IsKeyPressed(KEY_SIX)) {
    p.pointLights = !p.pointLights;
    changed = true;
  }
  if (changed)
    engine.Retro().SetParams(p);

  retro::PostParams q = engine.Post().Params();
  bool postChanged = false;

  if (IsKeyPressed(KEY_F7)) {
    q.filter = static_cast<retro::FilterMode>((static_cast<int>(q.filter) + 1) % 3);
    postChanged = true;
  }
  if (IsKeyPressed(KEY_F8)) {
    q.effects = !q.effects;
    postChanged = true;
  }
  if (postChanged)
    engine.Post().SetParams(q);
}

static void DrawMessageBox(const retro::Ui& ui, const std::string& text) {
  std::vector<std::string> lines;
  std::size_t start = 0;
  while (true) {
    const std::size_t nl = text.find('\n', start);
    lines.push_back(text.substr(start, nl == std::string::npos ? nl : nl - start));
    if (nl == std::string::npos)
      break;
    start = nl + 1;
  }

  constexpr float kSize = 10.0f, kLineH = 13.0f, kPadX = 7.0f, kPadY = 5.0f;
  float maxW = 0.0f;
  for (const std::string& l : lines)
    maxW = std::max(maxW, ui.TextWidth(l, kSize));

  const float boxW = maxW + kPadX * 2.0f;
  const float boxH = static_cast<float>(lines.size()) * kLineH + kPadY * 2.0f - 2.0f;
  const float x = (ui.Width() - boxW) * 0.5f;
  const float y = ui.Height() - boxH - 22.0f;

  ui.Rect(x, y, boxW, boxH, {0, 0, 0, 190});
  ui.RectLines(x, y, boxW, boxH, RAYWHITE);
  for (std::size_t i = 0; i < lines.size(); ++i) {
    ui.Text(lines[i], x + kPadX, y + kPadY + static_cast<float>(i) * kLineH - 1.0f, kSize, RAYWHITE,
            false);
  }
}

int main() {
  try {
    retro::EngineConfig config;
    config.title = "RetroEngine - Hito 16";
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
    bool showHelp = true;
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

          if (IsKeyPressed(KEY_F1))
            showHelp = !showHelp;

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
        },

        [&](retro::Ui& ui) {
          const float w = ui.Width();
          const float h = ui.Height();

          if (firstPerson) {
            const Color c = interactPrompt.empty() ? RAYWHITE : YELLOW;
            ui.Rect(w * 0.5f - 1.0f, h * 0.5f - 1.0f, 2.0f, 2.0f, c); // mira
          }
          if (!interactPrompt.empty()) {
            const float tw = ui.TextWidth(interactPrompt, 10.0f);
            ui.Text(interactPrompt, (w - tw) * 0.5f, h * 0.5f + 8.0f, 10.0f, YELLOW);
          }

          if (showHelp) {
            ui.Text("1 jitter 2 color 3 dither 4 niebla 5 afin 6 luces", 4, 3, 8, RAYWHITE);
            ui.Text("F5 recargar  F6 reset  F7 filtro  F8 acabado", 4, 13, 8, RAYWHITE);
            ui.Text(firstPerson ? "WASD mover  Shift correr  Espacio saltar  E usar"
                                : "Arrastrar/flechas: orbitar  Rueda: zoom",
                    4, 23, 8, RAYWHITE);
            ui.Text("Tab escena  F1 ayuda  F2 camara  F3 colisiones", 4, 33, 8, RAYWHITE);
          }

          ui.Text(TextFormat("Escena %d/%d: %s", static_cast<int>(scenes.Index()) + 1,
                             static_cast<int>(scenes.Count()), scenes.CurrentPath().c_str()),
                  4, h - 12.0f, 8.0f, RAYWHITE);

          if (state.HasMessage())
            DrawMessageBox(ui, state.MessageText());
        });
  } catch (const std::exception& e) {
    std::cerr << "Error fatal: " << e.what() << '\n';
    return 1;
  }
  return 0;
}