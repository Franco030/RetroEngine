#include "world/ActionRunner.hpp"

#include "game/Localization.hpp"
#include "player/Player.hpp"
#include "world/SceneManager.hpp"

#include <exception>
#include <iostream>
#include <string>
#include <unordered_map>
#include <cstddef>
#include <variant>

namespace retro {

namespace {

template <class... Ts> struct Overloaded : Ts... {
  using Ts::operator()...;
};
template <class... Ts> Overloaded(Ts...) -> Overloaded<Ts...>;

} // namespace

void ActionRunner::Update(float dt) {
  // Run puede anadir esperas nuevas mientras se recorre: primero se sacan las que tocan.
  std::vector<std::vector<Action>> due;
  for (auto it = pending_.begin(); it != pending_.end();) {
    it->left -= dt;
    if (it->left <= 0.0f) {
      due.push_back(std::move(it->actions));
      it = pending_.erase(it);
    } else {
      ++it;
    }
  }
  for (const auto& list : due)
    Run(list);
}

void ActionRunner::Run(const std::vector<Action>& actions) {
  for (std::size_t i = 0; i < actions.size(); ++i) {
    const Action& a = actions[i];
    if (const auto* w = std::get_if<action::Wait>(&a)) {
      pending_.push_back({w->seconds, std::vector<Action>(actions.begin() + static_cast<std::ptrdiff_t>(i) + 1,
                                                          actions.end())});
      return;
    }
    std::visit(
        Overloaded{
            [&](const action::GotoScene& g) {
              try {
                scenes_.GoToPath(g.scene, g.position, g.yaw, Interlude{g.dark, g.lines, g.music, g.musicVolume});
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
              state_.ShowMessage(ExpandFlags(lang::Tr(m.text), state_), m.seconds);
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
            [&](const action::PlayMusic& m) { audio_.PlayMusic(m.file, m.volume, m.fade, m.then); },
            [&](const action::StopMusic& m) { audio_.StopMusic(m.fade); },
            [&](const action::StartDialogue& d) { dialogues_.Start(d.id, state_); },
            [&](const action::LookAt& l) { shot_ = CameraShot{l.entity, l.point, l.speed, 0.0f}; },
            [&](const action::Scare& s) { shot_ = CameraShot{s.entity, s.point, s.speed, s.fov}; },
            [&](const action::LockMovement& l) { player_.SetRooted(l.locked); },
            [&](const action::TitleScreen&) { titleRequested_ = true; },
            [&](const action::Ride& r) { ride_ = r; },
            [&](const action::Shooting& s) { shooting_ = s; },
            [&](const action::BehindPlayer& b) {
              Scene* scene = scenes_.CurrentMut();
              Entity* e = scene ? scene->Find(b.entity) : nullptr;
              if (e) {
                scene->PlaceBehind(*e, player_.Feet(), player_.Yaw(), b.distance, b.side);
              } else {
                std::cerr << "behind_player: entidad inexistente '" << b.entity << "'\n";
              }
            },
            [&](const action::SetWeather& w) {
              if (w.rain)
                weather_.SetRain(*w.rain, w.fade);
              if (w.storm)
                weather_.SetStorm(*w.storm);
            },
            [&](const action::Lightning& l) { weather_.Strike(l.thunderDelay); },
            [&](const action::SetFog& f) { weather_.FadeFog(f.start, f.end, f.seconds); },
            [&](const action::Wait&) {}, // no llega aqui: se trata antes
            [&](const action::EnterBuilding& e) { scenes_.EnterBuilding(e.visit, e.fromRoof); },
            [&](const action::LeaveBuilding&) { scenes_.LeaveBuilding(); },
            [&](const action::ToRoof&) { scenes_.ToRoof(); },
            [&](const action::SetAmbient& a) { weather_.FadeAmbient(a.value, a.seconds); },
            [&](const action::ClearFlags& c) {
              std::unordered_map<std::string, int> kept;
              for (const std::string& k : c.keep) {
                if (const int v = state_.Get(k); v != 0)
                  kept[k] = v;
              }
              // los "once" de la escena en la que se esta se quedan: si no, lo que lo pidio (un
              // trigger de una vez) volveria a saltar en el fotograma siguiente
              const std::string here = "#";
              for (const char* tipo : {"once:", "interact:"}) {
                const std::string prefijo = tipo + scenes_.CurrentPath() + here;
                for (const auto& [k, v] : state_.Flags()) {
                  if (k.rfind(prefijo, 0) == 0)
                    kept[k] = v;
                }
              }
              state_.SetFlags(std::move(kept));
            },
        },
        a);
  }
}

} // namespace retro