#pragma once

#include "audio/AudioSystem.hpp"
#include "game/GameState.hpp"
#include "world/Dialogue.hpp"
#include "world/Trigger.hpp"

#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace retro {

class SceneManager;
class Player;

// Hacia donde llevar la camara del jugador (look_at o scare). fov 0 = sin acercar.
struct CameraShot {
  std::string entity;
  std::optional<Vector3> point;
  float speed = 6.0f;
  float fov = 0.0f;
};

// Ejecuta acciones sobre el resto del juego.
// Es el unico sitio que conoce a la vez al manager de escenas,
// al jugador y al estado.
class ActionRunner {
public:
  ActionRunner(SceneManager& scenes, Player& player, GameState& state, AudioSystem& audio,
               DialogueSystem& dialogues)
      : scenes_(scenes), player_(player), state_(state), audio_(audio), dialogues_(dialogues) {}

  void Run(const std::vector<Action>& actions);

  // La ultima mirada pedida (look_at o scare) desde la ultima llamada, y la olvida. La aplica
  // quien lleva la camara del jugador (main), que sabe a que altura se mira a una entidad.
  std::optional<CameraShot> TakeShot() noexcept {
    std::optional<CameraShot> out = std::move(shot_);
    shot_.reset();
    return out;
  }
  // Si se pidio title_screen desde la ultima llamada (y lo olvida): lo aplica main, que lleva las
  // pantallas del juego.
  bool TakeTitleRequest() noexcept { return std::exchange(titleRequested_, false); }
  // Lo ultimo pedido de montarse (ride) o de tiro al blanco (shooting), y lo olvida: lo aplica
  // main, que lleva al jugador y la camara.
  std::optional<action::Ride> TakeRide() noexcept { return std::exchange(ride_, std::nullopt); }
  std::optional<action::Shooting> TakeShooting() noexcept {
    return std::exchange(shooting_, std::nullopt);
  }

private:
  // Mismo orden que en la lista de inicializacion del constructor (-Wreorder).
  SceneManager& scenes_;
  Player& player_;
  GameState& state_;
  AudioSystem& audio_;
  DialogueSystem& dialogues_;
  std::optional<CameraShot> shot_;
  bool titleRequested_ = false;
  std::optional<action::Ride> ride_;
  std::optional<action::Shooting> shooting_;
};

} // namespace retro