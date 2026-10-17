#include "game/SaveGame.hpp"

#include "game/SaveStorage.hpp"

#include <raylib.h>

#include <nlohmann/json.hpp>

#include <cmath>
#include <ctime>
#include <map>
#include <stdexcept>
#include <utility>

namespace retro {

namespace {

using json = nlohmann::json;
namespace fs = std::filesystem;

std::string NowText() {
  const std::time_t t = std::time(nullptr);
  std::tm tm{};
#if defined(_MSC_VER)
  localtime_s(&tm, &t);
#elif defined(_WIN32)
  if (const std::tm* p = std::localtime(&t))
    tm = *p;
#else
  localtime_r(&t, &tm);
#endif
  char buf[32];
  std::strftime(buf, sizeof buf, "%Y-%m-%d %H:%M:%S", &tm);
  return buf;
}

std::string Serialize(const SaveData& d) {
  json j;
  j["version"] = SaveManager::kVersion;
  j["savedAt"] = d.savedAt.empty() ? NowText() : d.savedAt;
  j["playTime"] = d.playTime;
  j["scene"] = d.scene;
  j["player"] = {
      {"position", {d.position.x, d.position.y, d.position.z}}, {"yaw", d.yaw}, {"pitch", d.pitch}};

  json flags = json::object();
  for (const auto& [name, value] : std::map<std::string, int>(d.flags.begin(), d.flags.end())) {
    flags[name] = value;
  }
  j["flags"] = flags;
  return j.dump(2);
}

SaveData Deserialize(const std::string& text) {
  const json j = json::parse(text);
  if (!j.is_object())
    throw std::runtime_error("el archivo no es un objeto JSON");

  const int version = j.value("version", 0);
  if (version < 1)
    throw std::runtime_error("falta 'version' o no es valida");
  if (version > SaveManager::kVersion) {
    throw std::runtime_error("guardado de una version mas nueva (" + std::to_string(version) +
                             "); actualiza el juego");
  }

  SaveData d;
  d.scene = j.at("scene").get<std::string>();
  if (d.scene.empty())
    throw std::runtime_error("'scene' esta vacia");

  d.savedAt = j.value("savedAt", std::string{});
  d.playTime = j.value("playTime", 0.0);

  const json& player = j.at("player");
  const json& pos = player.at("position");
  if (!pos.is_array() || pos.size() != 3) {
    throw std::runtime_error("'player.position' debe ser [x, y, z]");
  }
  d.position = {pos[0].get<float>(), pos[1].get<float>(), pos[2].get<float>()};
  d.yaw = player.value("yaw", 0.0f);
  d.pitch = player.value("pitch", 0.0f);
  if (!std::isfinite(d.position.x) || !std::isfinite(d.position.y) ||
      !std::isfinite(d.position.z) || !std::isfinite(d.yaw) || !std::isfinite(d.pitch) ||
      !std::isfinite(d.playTime)) {
    throw std::runtime_error("el guardado contiene valores no finitos");
  }

  if (j.contains("flags")) {
    const json& flags = j.at("flags");
    if (!flags.is_object())
      throw std::runtime_error("'flags' debe ser un objeto");
    for (auto it = flags.begin(); it != flags.end(); ++it) {
      if (!it.value().is_number_integer()) {
        throw std::runtime_error("la bandera '" + it.key() + "' no es un entero");
      }
      d.flags[it.key()] = it.value().get<int>();
    }
  }
  return d;
}

} // namespace

SaveManager::SaveManager(fs::path directory) : dir_(std::move(directory)) {}

fs::path SaveManager::DefaultDirectory(const std::string& appName) {
  return savefile::DefaultDirectory(appName, fs::path(GetApplicationDirectory()));
}

fs::path SaveManager::SlotPath(int slot) const {
  return dir_ / (slot == kAutosaveSlot ? std::string("autosave.json")
                                       : "slot" + std::to_string(slot) + ".json");
}

bool SaveManager::Exists(int slot) const {
  std::error_code ec;
  const fs::path p = SlotPath(slot);
  return fs::exists(p, ec) || fs::exists(fs::path(p).concat(".bak"), ec);
}

void SaveManager::Write(int slot, const SaveData& data) const {
  savefile::WriteAtomic(SlotPath(slot), Serialize(data));
}

SaveData SaveManager::Read(int slot, bool* usedBackup) const {
  return savefile::ReadWithBackup(SlotPath(slot), Deserialize, usedBackup);
}

std::optional<SaveInfo> SaveManager::Peek(int slot) const {
  try {
    const SaveData d = Read(slot);
    return SaveInfo{d.scene, d.savedAt, d.playTime};
  } catch (...) {
    return std::nullopt;
  }
}

void SaveManager::Delete(int slot) const {
  std::error_code ec;
  const fs::path p = SlotPath(slot);
  fs::remove(p, ec);
  fs::remove(fs::path(p).concat(".bak"), ec);
  fs::remove(fs::path(p).concat(".tmp"), ec);
}

} // namespace retro