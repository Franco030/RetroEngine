#pragma once

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
struct Message {
  std::string text;
  float seconds = 3.0f;
};
struct SetVisible {
  std::string entity;
  bool visible = true;
};

} // namespace action

using Action = std::variant<action::GotoScene, action::Teleport, action::SetFlag, action::Message,
                            action::SetVisible>;

struct Condition {
  std::string flag;
  int equals = 1;
  bool negate = false;
};

enum class TriggerEvent { Enter, Exit, Stay };

struct Trigger {
  std::string name;
  WorldBox box;
  TriggerEvent on = TriggerEvent::Enter;
  bool once = false;
  bool marker = false;
  Color color{255, 200, 80, 255};
  std::vector<Condition> conditions;
  std::vector<Action> actions;

  bool inside = false;
};

} // namespace retro