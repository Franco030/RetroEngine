#include "world/Logic.hpp"

#include <algorithm>

namespace retro {

bool Evaluate(const Condition& c, const GameState& state) {
  switch (c.kind) {
  case Condition::Kind::Compare: {
    const int v = state.Get(c.flag);
    switch (c.op) {
    case CompareOp::Eq:
      return v == c.value;
    case CompareOp::Ne:
      return v != c.value;
    case CompareOp::Lt:
      return v < c.value;
    case CompareOp::Le:
      return v <= c.value;
    case CompareOp::Gt:
      return v > c.value;
    case CompareOp::Ge:
      return v >= c.value;
    }
    return false;
  }
  case Condition::Kind::All:
    return std::all_of(c.children.begin(), c.children.end(),
                       [&](const Condition& k) { return Evaluate(k, state); });
  case Condition::Kind::Any:
    return std::any_of(c.children.begin(), c.children.end(),
                       [&](const Condition& k) { return Evaluate(k, state); });

  case Condition::Kind::Not:
    return c.children.empty() || !Evaluate(c.children.front(), state);
  }

  return false;
}

bool AllHold(const std::vector<Condition>& conditions, const GameState& state) {
  return std::all_of(conditions.begin(), conditions.end(),
                     [&](const Condition& c) { return Evaluate(c, state); });
}

} // namespace retro