#pragma once

#include <string>
#include <vector>

namespace editor {

struct AssetCatalog {
  std::vector<std::string> models;
  std::vector<std::string> textures;
  std::vector<std::string> sounds;

  void Scan(const std::string& root);
};

} // namespace editor
