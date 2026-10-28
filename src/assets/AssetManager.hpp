#pragma once

#include "assets/Resources.hpp"

#include <memory>
#include <string>
#include <unordered_map>

namespace retro {

class AssetManager {
public:
  explicit AssetManager(std::string rootPath = "assets/");
  ~AssetManager() = default;

  AssetManager(const AssetManager&) = delete;
  AssetManager& operator=(const AssetManager&) = delete;
  AssetManager(AssetManager&&) = delete;
  AssetManager& operator=(AssetManager&&) = delete;

  std::shared_ptr<TextureResource> GetTexture(const std::string& relativePath);
  std::shared_ptr<ModelResource> GetModel(const std::string& relativePath);
  std::shared_ptr<ShaderResource> GetShader(const std::string& vertexPath,
                                            const std::string& fragmentPath);
  std::shared_ptr<FontResource> GetFont(const std::string& relativePath, int baseSize);
  std::shared_ptr<SoundResource> GetSound(const std::string& relativePath);

  void PurgeUnused();

  // Que ya esta en la cache (no costaria nada pedirlo).
  bool HasModel(const std::string& relativePath) const { return models_.contains(relativePath); }
  bool HasTexture(const std::string& relativePath) const {
    return textures_.contains(relativePath);
  }

  // Lo que se ha cargado de verdad (no estaba en cache) desde el ultimo ResetStats: para medir
  // cuanto cuesta cada cambio de escena.
  struct LoadStats {
    int models = 0, textures = 0, sounds = 0;
    double modelMs = 0.0, textureMs = 0.0, soundMs = 0.0;
  };
  const LoadStats& Stats() const noexcept { return stats_; }
  void ResetStats() noexcept { stats_ = {}; }

  void Clear();

  std::size_t TextureCount() const noexcept { return textures_.size(); }
  std::size_t ModelCount() const noexcept { return models_.size(); }
  std::size_t ShaderCount() const noexcept { return shaders_.size(); }
  std::size_t FontCount() const noexcept { return fonts_.size(); }
  std::size_t SoundCount() const noexcept { return sounds_.size(); }

  const std::string& Root() const noexcept { return root_; }

private:
  std::string Resolve(const std::string& relativePath) const;

  std::string root_;
  std::unordered_map<std::string, std::shared_ptr<TextureResource>> textures_;
  std::unordered_map<std::string, std::shared_ptr<ModelResource>> models_;
  std::unordered_map<std::string, std::shared_ptr<ShaderResource>> shaders_;
  std::unordered_map<std::string, std::shared_ptr<FontResource>> fonts_;
  std::unordered_map<std::string, std::shared_ptr<SoundResource>> sounds_;
  LoadStats stats_;
};

} // namespace retro