#pragma once

#include "physics/Collision.hpp"
#include "world/Logic.hpp"

#include <raylib.h>

#include <string>
#include <vector>

namespace retro {

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

  bool inside = false; // estado del frame anterior
};

} // namespace retro