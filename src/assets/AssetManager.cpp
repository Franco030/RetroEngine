#include "assets/AssetManager.hpp"

#include <chrono>
#include <utility>

namespace retro {

namespace {

// Contador de lo que se carga de verdad: cuantos y cuanto tardan.
struct Counter {
  int* count = nullptr;
  double* ms = nullptr;
};

template <typename T, typename... Args>
std::shared_ptr<T> GetOrLoad(std::unordered_map<std::string, std::shared_ptr<T>>& cache,
                             Counter counter, const std::string& key, Args&&... args) {
  if (auto it = cache.find(key); it != cache.end()) {
    return it->second;
  }
  const auto start = std::chrono::steady_clock::now();
  auto asset = std::make_shared<T>(std::forward<Args>(args)...);
  cache.emplace(key, asset);
  if (counter.count) {
    ++*counter.count;
    *counter.ms +=
        std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start).count();
  }
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
  return GetOrLoad(textures_, {&stats_.textures, &stats_.textureMs}, relativePath,
                   Resolve(relativePath));
}

std::shared_ptr<ModelResource> AssetManager::GetModel(const std::string& relativePath) {
  return GetOrLoad(models_, {&stats_.models, &stats_.modelMs}, relativePath,
                   Resolve(relativePath));
}

std::shared_ptr<ShaderResource> AssetManager::GetShader(const std::string& vertexPath,
                                                        const std::string& fragmentPath) {
  const std::string key = vertexPath + "|" + fragmentPath;
  return GetOrLoad(shaders_, {}, key, Resolve(vertexPath), Resolve(fragmentPath));
}

std::shared_ptr<FontResource> AssetManager::GetFont(const std::string& relativePath, int baseSize) {
  const std::string key = relativePath + "@" + std::to_string(baseSize);
  return GetOrLoad(fonts_, {}, key, Resolve(relativePath), baseSize);
}

std::shared_ptr<SoundResource> AssetManager::GetSound(const std::string& relativePath) {
  return GetOrLoad(sounds_, {&stats_.sounds, &stats_.soundMs}, relativePath,
                   Resolve(relativePath));
}

void AssetManager::PurgeUnused() {
  auto unused = [](const auto& entry) { return entry.second.use_count() == 1; };
  std::erase_if(textures_, unused);
  std::erase_if(models_, unused);
  std::erase_if(shaders_, unused);
  std::erase_if(fonts_, unused);
  std::erase_if(sounds_, unused);
}

void AssetManager::Clear() {
  textures_.clear();
  models_.clear();
  shaders_.clear();
  fonts_.clear();
  sounds_.clear();
}

} // namespace retro