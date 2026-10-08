#include "world/ActionRunner.hpp"
#include "player/Player.hpp"
#include "world/SceneManager.hpp"

#include <exception>
#include <iostream>
#include <variant>

namespace retro {

namespace {

template <class... Ts> struct Overloaded : Ts... {
  using Ts::operator()...;
};

template <class... Ts> Overloaded(Ts...) -> Overloaded<Ts...>;

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
                          [&](const action::Message& m) { state_.ShowMessage(m.text, m.seconds); },
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