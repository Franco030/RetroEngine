#pragma once

#include <string>
#include <vector>

namespace editor {

struct AssetCatalog {
  std::vector<std::string> models;
  std::vector<std::string> textures;
  std::vector<std::string> sounds;

  // Los assets del juego ('root') y los del motor ('engineRoot', los que el juego no tapa).
  void Scan(const std::string& root, const std::string& engineRoot);
};

} // namespace editor
