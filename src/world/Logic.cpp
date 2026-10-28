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

std::string ExpandFlags(const std::string& text, const GameState& state) {
  std::string out;
  out.reserve(text.size());
  for (std::size_t i = 0; i < text.size(); ++i) {
    if (text[i] == '{') {
      const std::size_t close = text.find('}', i + 1);
      if (close != std::string::npos) {
        out += std::to_string(state.Get(text.substr(i + 1, close - i - 1)));
        i = close;
        continue;
      }
    }
    out += text[i];
  }
  return out;
}

bool AllHold(const std::vector<Condition>& conditions, const GameState& state) {
  return std::all_of(conditions.begin(), conditions.end(),
                     [&](const Condition& c) { return Evaluate(c, state); });
}

} // namespace retro