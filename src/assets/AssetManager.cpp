#include "assets/AssetManager.hpp"

#include <utility>

namespace retro {

namespace {

template <typename T, typename... Args>
std::shared_ptr<T> GetOrLoad(std::unordered_map<std::string, std::shared_ptr<T>>& cache,
                             const std::string& key, Args&&... args) {
  if (auto it = cache.find(key); it != cache.end()) {
    return it->second;
  }
  auto asset = std::make_shared<T>(std::forward<Args>(args)...);
  cache.emplace(key, asset);
  return asset;
}

} // namespace

AssetManager::AssetManager(std::string rootPath) : root_(std::move(rootPath)) {
  if (!root_.empty() && root_.back() != '/' && root_.back() != '\\') {
    root_.push_back('/');
  }
}

std::string AssetManager::Resolve(const std::string& relativePath) const {
  return relativePath.empty() ? std::string{} : root_ + relativePath;
}

std::shared_ptr<TextureResource> AssetManager::GetTexture(const std::string& relativePath) {
  return GetOrLoad(textures_, relativePath, Resolve(relativePath));
}

std::shared_ptr<ModelResource> AssetManager::GetModel(const std::string& relativePath) {
  return GetOrLoad(models_, relativePath, Resolve(relativePath));
}

std::shared_ptr<ShaderResource> AssetManager::GetShader(const std::string& vertexPath,
                                                        const std::string& fragmentPath) {
  const std::string key = vertexPath + "|" + fragmentPath;
  return GetOrLoad(shaders_, key, Resolve(vertexPath), Resolve(fragmentPath));
}

std::shared_ptr<FontResource> AssetManager::GetFont(const std::string& relativePath, int baseSize) {
  const std::string key = relativePath + "@" + std::to_string(baseSize);
  return GetOrLoad(fonts_, key, Resolve(relativePath), baseSize);
}

void AssetManager::PurgeUnused() {
  auto unused = [](const auto& entry) { return entry.second.use_count() == 1; };
  std::erase_if(textures_, unused);
  std::erase_if(models_, unused);
  std::erase_if(shaders_, unused);
  std::erase_if(fonts_, unused);
}

void AssetManager::Clear() {
  textures_.clear();
  models_.clear();
  shaders_.clear();
  fonts_.clear();
}

} // namespace retro