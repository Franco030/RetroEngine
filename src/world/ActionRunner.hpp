#pragma once

#include "game/GameState.hpp"
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
  ActionRunner(SceneManager& scenes, Player& player, GameState& state)
      : scenes_(scenes), player_(player), state_(state) {}

  void Run(const std::vector<Action>& actions);

private:
  SceneManager& scenes_;
  Player& player_;
  GameState& state_;
};

} // namespace retro