#include "game/Settings.hpp"

#include "game/SaveStorage.hpp"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <exception>
#include <iostream>

namespace retro {

using json = nlohmann::json;

Settings LoadSettings(const std::filesystem::path& file) {
  Settings s;
  if (!std::filesystem::exists(file))
    return s;
  try {
    const json j = json::parse(savefile::ReadText(file));
    s.volume = std::clamp(j.value("volume", s.volume), 0.0f, 1.0f);
    s.sensitivity = std::clamp(j.value("sensitivity", s.sensitivity), 1, 10);
    s.fullscreen = j.value("fullscreen", s.fullscreen);
    s.filter = static_cast<FilterMode>(std::clamp(j.value("filter", static_cast<int>(s.filter)), 0, 2));
    s.effects = j.value("effects", s.effects);
  } catch (const std::exception& e) {
    std::cerr << "Ajustes: " << e.what() << " (se usan los de por defecto)\n";
    return Settings{};
  }
  return s;
}

void SaveSettings(const std::filesystem::path& file, const Settings& s) {
  const json j = {{"volume", s.volume},
                  {"sensitivity", s.sensitivity},
                  {"fullscreen", s.fullscreen},
                  {"filter", static_cast<int>(s.filter)},
                  {"effects", s.effects}};
  savefile::WriteAtomic(file, j.dump(2));
}

} // namespace retro
