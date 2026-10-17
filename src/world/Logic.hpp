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

} // namespace action

using Action =
    std::variant<action::GotoScene, action::Teleport, action::SetFlag, action::AddFlag,
                 action::Message, action::PlaySound, action::PlayMusic, action::StopMusic>;

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