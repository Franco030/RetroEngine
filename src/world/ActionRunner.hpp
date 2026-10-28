#pragma once

#include "audio/AudioSystem.hpp"
#include "game/GameState.hpp"
#include "world/Dialogue.hpp"
#include "world/Trigger.hpp"

#include <vector>

namespace retro {

class SceneManager;
class Player;

// Ejecuta acciones sobre el resto del juego.
// Es el unico sitio que conoce a la vez al manager de escenas,
// al jugador y al estado.
class ActionRunner {
public:
  ActionRunner(SceneManager& scenes, Player& player, GameState& state, AudioSystem& audio,
               DialogueSystem& dialogues)
      : scenes_(scenes), player_(player), state_(state), audio_(audio), dialogues_(dialogues) {}

  void Run(const std::vector<Action>& actions);

private:
  // Mismo orden que en la lista de inicializacion del constructor (-Wreorder).
  SceneManager& scenes_;
  Player& player_;
  GameState& state_;
  AudioSystem& audio_;
  DialogueSystem& dialogues_;
};

} // namespace retro