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

} // namespace

void ActionRunner::Run(const std::vector<Action>& actions) {
  for (const Action& a : actions) {
    std::visit(
        Overloaded{
            [&](const action::GotoScene& g) {
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
            [&](const action::AddFlag& f) { state_.Set(f.name, state_.Get(f.name) + f.amount); },
            [&](const action::Message& m) {
              state_.ShowMessage(ExpandFlags(m.text, state_), m.seconds);
            },
            [&](const action::PlaySound& s) {
              if (s.at) {
                audio_.PlaySfxAt(s.file, s.volume, *s.at, s.radius, s.minDistance);
              } else if (!s.atEntity.empty()) {
                Scene* scene = scenes_.CurrentMut();
                Entity* e = scene ? scene->Find(s.atEntity) : nullptr;
                if (e) {
                  audio_.PlaySfxAt(s.file, s.volume, e->transform.position, s.radius,
                                   s.minDistance);
                } else {
                  std::cerr << "play_sound: entidad inexistente '" << s.atEntity << "'\n";
                }
              } else {
                audio_.PlaySfx(s.file, s.volume);
              }
            },
            [&](const action::PlayMusic& m) { audio_.PlayMusic(m.file, m.volume, m.fade); },
            [&](const action::StopMusic& m) { audio_.StopMusic(m.fade); },
            [&](const action::StartDialogue& d) { dialogues_.Start(d.id, state_); },
            [&](const action::LookAt& l) { shot_ = CameraShot{l.entity, l.point, l.speed, 0.0f}; },
            [&](const action::Scare& s) { shot_ = CameraShot{s.entity, s.point, s.speed, s.fov}; },
        },
        a);
  }
}

} // namespace retro