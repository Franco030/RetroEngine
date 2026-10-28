#include <raylib.h>
#include <raymath.h>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <exception>
#include <iostream>
#include <optional>
#include <string>
#include <variant>
#include <vector>

#include "audio/AudioSystem.hpp"
#include "audio/Footsteps.hpp"
#include "core/RetroEngine.hpp"
#include "game/GameState.hpp"
#include "game/SaveGame.hpp"
#include "graphics/DebugDraw.hpp"
#include "graphics/RetroCamera.hpp"
#include "graphics/ShaderScope.hpp"
#include "physics/Collision.hpp"
#include "player/Player.hpp"
#include "ui/Ui.hpp"
#include "world/ActionRunner.hpp"
#include "world/Dialogue.hpp"
#include "world/SceneManager.hpp"

static void HandleDebugKeys(retro::RetroEngine& engine) {
  static bool jitter = true, lowColor = true, dither = true;

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
  if (IsKeyPressed(KEY_M)) {
    engine.Audio().SetMasterVolume(engine.Audio().MasterVolume() > 0.0f ? 0.0f : 1.0f);
  }
  if (postChanged)
    engine.Post().SetParams(q);
}

// Subtitulo sobre la imagen, sin cuadro: blanco con contorno negro, centrado abajo. Solo se
// dibujan los 'visible' primeros bytes (el texto se escribe letra a letra), pero cada linea se
// coloca segun su ancho completo, asi no se mueve mientras crece. Encima va quien habla (si hay)
// y, con 'more', un triangulo que parpadea al final: E pasa de pagina.
static void DrawSubtitle(const retro::Ui& ui, const std::string& text, std::size_t visible,
                         const std::string& speaker, bool more) {
  struct Line {
    std::string text;
    std::size_t start; // posicion de la linea dentro de 'text'
  };
  std::vector<Line> lines;
  std::size_t start = 0;
  while (true) {
    const std::size_t nl = text.find('\n', start);
    lines.push_back({text.substr(start, nl == std::string::npos ? nl : nl - start), start});
    if (nl == std::string::npos)
      break;
    start = nl + 1;
  }

  constexpr float kSize = 11.0f, kLineH = 13.0f, kBorde = 0.75f;
  auto outlined = [&](const std::string& s, float x, float y, float size, Color color) {
    for (int dx = -1; dx <= 1; ++dx) { // contorno: el mismo texto en negro alrededor
      for (int dy = -1; dy <= 1; ++dy) {
        if (dx != 0 || dy != 0)
          ui.Text(s, x + dx * kBorde, y + dy * kBorde, size, BLACK, false);
      }
    }
    ui.Text(s, x, y, size, color, false);
  };

  const float y0 = ui.Height() - 20.0f - static_cast<float>(lines.size()) * kLineH;
  if (!speaker.empty()) {
    constexpr float kSpeakerSize = 9.0f;
    const float x = (ui.Width() - ui.TextWidth(speaker, kSpeakerSize)) * 0.5f;
    outlined(speaker, x, y0 - 11.0f, kSpeakerSize, Color{214, 178, 110, 255});
  }
  float endX = 0.0f, endY = 0.0f;
  for (std::size_t i = 0; i < lines.size(); ++i) {
    if (visible <= lines[i].start)
      break;
    const std::string shown = lines[i].text.substr(0, visible - lines[i].start);
    const float x = (ui.Width() - ui.TextWidth(lines[i].text, kSize)) * 0.5f;
    const float y = y0 + static_cast<float>(i) * kLineH;
    outlined(shown, x, y, kSize, RAYWHITE);
    endX = x + ui.TextWidth(lines[i].text, kSize);
    endY = y;
  }

  if (more && visible >= text.size() && std::fmod(GetTime(), 0.8) < 0.5) {
    // Triangulo hacia abajo (filas de 7, 5, 3 y 1 pixeles): primero todo el contorno negro y
    // luego el blanco, si no el contorno de cada fila tapa la anterior.
    const float x = endX + 4.0f, y = endY + 3.0f;
    for (int r = 0; r < 4; ++r)
      ui.Rect(x - 1.0f + r, y - 1.0f + r, 9.0f - 2.0f * r, 3.0f, BLACK);
    for (int r = 0; r < 4; ++r)
      ui.Rect(x + r, y + r, 7.0f - 2.0f * r, 1.0f, RAYWHITE);
  }
}

// Sonido de las letras: un chasquido de estatica cada dos letras (no en espacios), alternando
// tres variantes para que no suene a metralleta.
class LetterSounds {
public:
  void Update(const retro::GameState& state, retro::AudioSystem& audio) {
    if (!state.HasMessage())
      return;
    if (state.MessageId() != id_) {
      id_ = state.MessageId();
      shown_ = 0;
    }
    const std::string& text = state.MessageText();
    const std::size_t now = state.VisibleBytes();
    for (; shown_ < now; ++shown_) {
      const unsigned char c = static_cast<unsigned char>(text[shown_]);
      if (c == ' ' || c == '\n' || (c & 0xC0) == 0x80)
        continue;
      if (++letters_ % 2 == 0) {
        static const char* const kFiles[] = {"audio/letra_1.wav", "audio/letra_2.wav",
                                             "audio/letra_3.wav"};
        audio.PlaySfx(kFiles[(letters_ / 2 + shown_) % 3], 0.32f);
      }
    }
  }

private:
  unsigned id_ = 0;
  std::size_t shown_ = 0;
  unsigned letters_ = 0;
};

// Punto al que mirar durante un dialogo: el de la caja de interaccion de la entidad (la cara si
// es alta como una persona; si no, su centro).
static Vector3 DialogueFocus(const retro::Entity& e) {
  const retro::WorldBox b = retro::MakeWorldBox(e.interact->box, e.transform);
  const float alto = b.top - b.bottom;
  const float y = alto > 1.4f ? b.top - 0.4f : (b.top + b.bottom) * 0.5f;
  return {b.center.x, y, b.center.z};
}

// Si las acciones dicen algo (un mensaje o un dialogo): entonces la camara mira a quien habla.
static bool Speaks(const std::vector<retro::Action>& actions) {
  return std::any_of(actions.begin(), actions.end(), [](const retro::Action& a) {
    return std::holds_alternative<retro::action::Message>(a) ||
           std::holds_alternative<retro::action::StartDialogue>(a);
  });
}

// RetroEngine [--scene scenes/x.json]: escena con la que empieza (por defecto la demo).
int main(int argc, char** argv) {
  std::string primera = "scenes/demo.json";
  for (int i = 1; i + 1 < argc; ++i) {
    if (std::string(argv[i]) == "--scene")
      primera = argv[i + 1];
  }

  try {
    retro::EngineConfig config;
    config.title = "RetroEngine";
    retro::RetroEngine engine(config);
    retro::RetroCamera camera;

    retro::SceneManager scenes(engine, camera, {primera});
    scenes.LoadNow(0);
    scenes.FadeInFromBlack();

    retro::Player player;
    retro::GameState state;
    retro::DialogueSystem dialogues(engine.Assets().Root());
    retro::ActionRunner runner(scenes, player, state, engine.Audio(), dialogues);

    std::vector<retro::WorldBox> colliders;
    std::vector<retro::Action> firedActions;
    std::uint64_t seenGeneration = scenes.Generation();
    bool firstPerson = true;
    std::optional<Vector3> dialogueFocus; // a quien se mira mientras dura un dialogo
    LetterSounds letterSounds;
    float dialogueAge = 0.0f; // tiempo en la pagina actual
    unsigned pageId = 0;
    bool showColliders = false;
    bool showHelp = false;
    retro::FootstepTracker footsteps;

    retro::SaveManager saves(retro::SaveManager::DefaultDirectory("RetroEngine"));
    double playTime = 0.0f;
    std::optional<retro::SaveData> pendingLoad;
    const auto initialFlags = state.Flags();
    const std::uint64_t initialGeneration = scenes.Generation();

    std::string interactPrompt;

    auto spawnPlayer = [&]() {
      if (const retro::Scene* s = scenes.Current()) {
        player.Spawn(s->spawn.position, s->spawn.yaw);
        footsteps.Reset(s->spawn.position);
      }
    };
    spawnPlayer();
    player.SetActive(true);

    auto snapshot = [&]() {
      retro::SaveData d;
      d.scene = scenes.CurrentPath();
      d.position = player.Feet();
      d.yaw = player.Yaw();
      d.pitch = player.Pitch();
      d.playTime = playTime;
      d.flags = state.Flags();
      return d;
    };

    auto saveTo = [&](int slot, const char* confirmation, bool allowDuringTransition = false) {
      if (scenes.Transitioning() && !allowDuringTransition) {
        if (confirmation)
          state.ShowMessage("No se puede guardar durante un cambio de escena", 2.5f);
        return;
      }
      try {
        saves.Write(slot, snapshot());
        if (confirmation)
          state.ShowMessage(confirmation, 2.0f);
      } catch (const std::exception& e) {
        std::cerr << "Guardado: " << e.what() << '\n';
        if (confirmation)
          state.ShowMessage("Error al guardar (ver consola)", 3.5f);
      }
    };

    auto loadFrom = [&](int slot) {
      if (scenes.Transitioning() || pendingLoad)
        return;
      try {
        bool backup = false;
        retro::SaveData data = saves.Read(slot, &backup);
        scenes.GoToPath(data.scene, data.position, data.yaw);
        pendingLoad = std::move(data);
        if (backup)
          std::cerr << "Carga: el guardado estaba danado; se uso la copia de seguridad\n";
      } catch (const std::exception& e) {
        std::cerr << "Carga: " << e.what() << '\n';
        state.ShowMessage("No se pudo cargar la partida (ver consola)", 3.5f);
      }
    };

    if (saves.Exists(1) || saves.Exists(retro::SaveManager::kAutosaveSlot)) {
      state.ShowMessage("Hay una partida guardada.\nF10: cargar    Shift+F10: autoguardado", 6.0f);
    }

    engine.Run(
        [&](float dt) {
          playTime += dt;

          if (IsKeyPressed(KEY_F9))
            saveTo(1, "Partida guardada");

          if (IsKeyPressed(KEY_F10)) {
            const bool shift = IsKeyDown(KEY_LEFT_SHIFT) || IsKeyDown(KEY_RIGHT_SHIFT);
            loadFrom(shift ? retro::SaveManager::kAutosaveSlot : 1);
          }

          if (IsKeyPressed(KEY_TAB)) {
            if (IsKeyDown(KEY_LEFT_SHIFT) || IsKeyDown(KEY_RIGHT_SHIFT)) {
              scenes.Previous();
            } else {
              scenes.Next();
            }
          }

          if (IsKeyPressed(KEY_F5)) {
            try {
              if (dialogues.Active())
                state.ClearMessage();
              dialogues.Reload(); // tambien se releen los dialogos
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

          scenes.SetFocus(player.Feet());
          scenes.Update(dt);
          state.Update(dt);
          letterSounds.Update(state, engine.Audio());

          if (scenes.Generation() != seenGeneration) {
            seenGeneration = scenes.Generation();
            spawnPlayer();

            if (pendingLoad) {
              state.Clear();
              state.SetFlags(pendingLoad->flags);
              player.SetPitch(pendingLoad->pitch);
              playTime = pendingLoad->playTime;
              pendingLoad.reset();
              state.ShowMessage("Partida cargada", 2.0f);
            } else {
              saveTo(retro::SaveManager::kAutosaveSlot, nullptr, true);
            }
          } else if (pendingLoad && !scenes.Transitioning()) {
            pendingLoad.reset();
            state.ShowMessage("No se pudo cargar la escena guardada (ver consola)", 3.5f);
          }

          if (retro::Scene* s = scenes.CurrentMut())
            s->RefreshVisibility(state);

          colliders.clear();
          if (const retro::Scene* s = scenes.Current())
            s->CollectColliders(colliders);

          // Dialogo: un mensaje que salio de una interaccion, o un dialogo abierto. Mientras se
          // lee, el jugador se queda quieto (mirando a quien habla si se sabe quien es); un
          // anuncio suelto (trigger, sistema) no lo retiene.
          dialogues.Update(state);
          if (dialogueFocus && !state.HasMessage())
            dialogueFocus.reset();
          const bool reading = dialogueFocus.has_value() || dialogues.Active();
          if (state.MessageId() != pageId) { // cada pagina nueva da su respiro
            pageId = state.MessageId();
            dialogueAge = 0.0f;
          }
          dialogueAge = reading ? dialogueAge + dt : 0.0f;
          player.SetFrozen(reading);

          if (firstPerson) {
            if (!scenes.Transitioning())
              player.Update(dt, colliders);
            if (dialogueFocus)
              player.TurnTowards(*dialogueFocus, dt);
            player.ApplyToCamera(camera);
          } else {
            camera.UpdateOrbit(dt);
          }

          footsteps.Update(player.Feet(), player.Grounded(), IsKeyDown(KEY_LEFT_SHIFT),
                           firstPerson && !scenes.Transitioning(), engine.Audio());
          scenes.UpdateAudio(dt);

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
              if (reading) {
                // E durante un dialogo: si aun se esta escribiendo, lo muestra entero; si no,
                // pasa de pagina o lo cierra (con un respiro, para no saltarlo sin leer). Las
                // acciones del final se ejecutan al cerrar (pueden abrir otro dialogo).
                if (IsKeyPressed(KEY_E)) {
                  if (!state.FullyShown()) {
                    state.RevealAll();
                  } else if (dialogueAge > 0.3f) {
                    if (dialogues.Active()) {
                      const std::vector<retro::Action> end = dialogues.Advance(state);
                      runner.Run(end);
                    } else {
                      state.ClearMessage();
                    }
                    if (!state.HasMessage())
                      dialogueFocus.reset();
                  }
                }
              } else if (hit.entity) {
                interactPrompt = "E: " + hit.entity->interact->prompt;
                if (IsKeyPressed(KEY_E)) {
                  firedActions.clear();
                  s->Interact(*hit.entity, state, firedActions);
                  runner.Run(firedActions);
                  if (Speaks(firedActions) && state.HasMessage())
                    dialogueFocus = DialogueFocus(*hit.entity);
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

          if (firstPerson && !dialogueFocus && !dialogues.Active()) {
            const Color c = interactPrompt.empty() ? RAYWHITE : YELLOW;
            ui.Rect(w * 0.5f - 1.0f, h * 0.5f - 1.0f, 2.0f, 2.0f, c); // mira
          }
          if (!interactPrompt.empty()) {
            const float tw = ui.TextWidth(interactPrompt, 10.0f);
            ui.Text(interactPrompt, (w - tw) * 0.5f, h * 0.5f + 8.0f, 10.0f, YELLOW);
          }

          if (showHelp) {
            ui.Text("1 jitter 2 color 3 dither 4 niebla 6 luces", 4, 3, 8, RAYWHITE);
            ui.Text("F5 recargar  F6 reset  F7 filtro  F8 acabado M sonido", 4, 13, 8, RAYWHITE);
            ui.Text(firstPerson ? "WASD mover  Shift correr  Espacio saltar  E usar"
                                : "Arrastrar/flechas: orbitar  Rueda: zoom",
                    4, 23, 8, RAYWHITE);
            ui.Text("F9 guardar  F10 cargar  Shift+F10 autoguardado", 4, 43, 8, RAYWHITE);
            ui.Text("Tab escena  F1 ayuda  F2 camara  F3 colisiones", 4, 33, 8, RAYWHITE);

            ui.Text(TextFormat("Escena %d/%d: %s", static_cast<int>(scenes.Index()) + 1,
                               static_cast<int>(scenes.Count()), scenes.CurrentPath().c_str()),
                    4, h - 12.0f, 8.0f, RAYWHITE);
          }

          if (state.HasMessage())
            DrawSubtitle(ui, state.MessageText(), state.VisibleBytes(), state.MessageSpeaker(),
                         dialogues.Active());
        });

    if (scenes.Generation() != initialGeneration || state.Flags() != initialFlags) {
      saveTo(retro::SaveManager::kAutosaveSlot, nullptr, true);
    }
  } catch (const std::exception& e) {
    std::cerr << "Error fatal: " << e.what() << '\n';
    return 1;
  }
  return 0;
}