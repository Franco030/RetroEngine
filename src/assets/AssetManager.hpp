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

  void PurgeUnused();

  void Clear();

  std::size_t TextureCount() const noexcept { return textures_.size(); }
  std::size_t ModelCount() const noexcept { return models_.size(); }
  std::size_t ShaderCount() const noexcept { return shaders_.size(); }

private:
  std::string Resolve(const std::string& relativePath) const;

  std::string root_;
  std::unordered_map<std::string, std::shared_ptr<TextureResource>> textures_;
  std::unordered_map<std::string, std::shared_ptr<ModelResource>> models_;
  std::unordered_map<std::string, std::shared_ptr<ShaderResource>> shaders_;
};

} // namespace retro