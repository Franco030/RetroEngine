#include "world/ActionRunner.hpp"

#include "player/Player.hpp"
#include "world/SceneManager.hpp"

#include <exception>
#include <iostream>
#include <string>
#include <variant>

namespace retro {

namespace {

template <class... Ts> struct Overloaded : Ts... {
  using Ts::operator()...;
};
template <class... Ts> Overloaded(Ts...) -> Overloaded<Ts...>;

// Reemplaza {nombre} por el valor de la bandera (0 si nunca se fijó).
std::string Expand(const std::string& text, const GameState& state) {
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

} // namespace

void ActionRunner::Run(const std::vector<Action>& actions) {
  for (const Action& a : actions) {
    std::visit(Overloaded{[&](const action::GotoScene& g) {
                            try {
                              scenes_.GoToPath(g.scene, g.position, g.yaw);
                            } catch (const std::exception& e) {
                              std::cerr << "goto_scene: " << e.what() << '\n';
                            }
                          },
                          [&](const action::Teleport& t) {
                            player_.Spawn(t.position, t.yaw.value_or(player_.Yaw()));
                          },
                          [&](const action::SetFlag& f) { state_.Set(f.name, f.value); },
                          [&](const action::AddFlag& f) {
                            state_.Set(f.name, state_.Get(f.name) + f.amount);
                          },
                          [&](const action::Message& m) {
                            state_.ShowMessage(Expand(m.text, state_), m.seconds);
                          },
                          [&](const action::SetVisible& v) {
                            Scene* scene = scenes_.CurrentMut();
                            Entity* e = scene ? scene->Find(v.entity) : nullptr;
                            if (e) {
                              e->visible = v.visible;
                            } else {
                              std::cerr << "set_visible: entidad inexistente '" << v.entity
                                        << "'\n";
                            }
                          }},
               a);
  }
}

} // namespace retro