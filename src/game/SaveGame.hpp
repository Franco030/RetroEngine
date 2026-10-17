#pragma once

#include <raylib.h>

#include <filesystem>
#include <optional>
#include <string>
#include <unordered_map>

namespace retro {

struct SaveData {
  std::string scene;
  Vector3 position{0.0f, 0.0f, 0.0f};
  float yaw = 0.0f;
  float pitch = 0.0f;
  double playTime = 0.0;
  std::string savedAt;
  std::unordered_map<std::string, int> flags;
};

struct SaveInfo {
  std::string scene;
  std::string savedAt;
  double playTime = 0.0;
};

class SaveManager {
public:
  static constexpr int kVersion = 1;
  static constexpr int kAutosaveSlot = 0;

  explicit SaveManager(std::filesystem::path directory);

  static std::filesystem::path DefaultDirectory(const std::string& appName);

  const std::filesystem::path& Directory() const noexcept { return dir_; }
  std::filesystem::path SlotPath(int slot) const;
  bool Exists(int slot) const;

  void Write(int slot, const SaveData& data) const;
  SaveData Read(int slot, bool* usedBackup = nullptr) const;

  std::optional<SaveInfo> Peek(int slot) const;
  void Delete(int slot) const;

private:
  std::filesystem::path dir_;
};

} // namespace retro