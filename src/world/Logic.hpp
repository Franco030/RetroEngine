#pragma once

#include "game/GameState.hpp"
#include "physics/Collision.hpp"

#include <raylib.h>

#include <optional>
#include <string>
#include <variant>
#include <vector>

namespace retro {

namespace action {

struct GotoScene {
  std::string scene;
  std::optional<Vector3> position;
  std::optional<float> yaw;
};

struct Teleport {
  Vector3 position{};
  std::optional<float> yaw;
};

struct SetFlag {
  std::string name;
  int value = 1;
};

struct AddFlag {
  std::string name;
  int amount = 1;
};

struct Message {
  std::string text;
  float seconds = 3.0f;
};

struct PlaySound {
  std::string file;
  float volume = 1.0f;
  std::optional<Vector3> at;
  std::string atEntity;
  float radius = 14.0f;
  float minDistance = 1.0f;
};

struct PlayMusic {
  std::string file;
  float volume = 0.6f;
  float fade = 1.0f;
};

struct StopMusic {
  float fade = 1.0f;
};

// Abre el dialogo assets/dialogues/<id>.json (ver DialogueSystem).
struct StartDialogue {
  std::string id;
};

// Las dos giran la vista del jugador hacia una entidad (o un punto) mientras dure el mensaje o
// dialogo que se este leyendo; 'speed' es lo rapido que gira.
//
// Mirar: suave y sin acercar (un objeto, alguien que habla).
struct LookAt {
  std::string entity;
  std::optional<Vector3> point;
  float speed = 6.0f;
};

// Susto de camara: giro de golpe y acercamiento (la vista se estrecha hasta 'fov' grados).
struct Scare {
  std::string entity;
  std::optional<Vector3> point;
  float speed = 20.0f;
  float fov = 30.0f;
};

} // namespace action

using Action =
    std::variant<action::GotoScene, action::Teleport, action::SetFlag, action::AddFlag,
                 action::Message, action::PlaySound, action::PlayMusic, action::StopMusic,
                 action::StartDialogue, action::LookAt, action::Scare>;

enum class CompareOp { Eq, Ne, Lt, Le, Gt, Ge };

// Arbol de condiciones: una comparacion de bandera o un grupo
struct Condition {
  enum class Kind { Compare, All, Any, Not };

  Kind kind = Kind::Compare;

  // Compare
  std::string flag;
  CompareOp op = CompareOp::Eq;
  int value = 1;

  std::vector<Condition> children;
};

bool Evaluate(const Condition& condition, const GameState& state);

// Lista vacia = se cumple. Tdoas deben cumplirse
bool AllHold(const std::vector<Condition>& conditions, const GameState& state);

// Reemplaza {nombre} por el valor de la bandera (0 si nunca se fijo).
std::string ExpandFlags(const std::string& text, const GameState& state);

// Lo que pasa cuando el jugador MIRA una entidad: esta en el centro de la vista (a menos de
// 'angle' grados), a menos de 'range' metros y sin nada solido en medio. 'onSeen' ("do") se
// ejecuta al verla y 'onLookAway' ("away") cuando deja de mirarla despues de haberla visto. Cada
// uno una sola vez por visita a la escena (lo que deba durar, con banderas y "showIf").
struct Sight {
  float range = 15.0f;
  float angle = 20.0f;
  std::vector<Action> onSeen;
  std::vector<Action> onLookAway;
  bool seen = false;    // ya la vio (se ejecuto onSeen)
  bool away = false;    // ya aparto la vista (se ejecuto onLookAway)
};

// Lo que ocurre al pulsar E sobre una entidad
struct Interaction {
  std::string prompt = "Interactuar";
  float range = 2.5f;
  bool once = false;
  BoxCollider box;
  std::vector<Condition> conditions;
  std::vector<Action> actions;
};

} // namespace retro