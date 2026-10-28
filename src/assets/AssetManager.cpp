#include "assets/AssetManager.hpp"

#include <algorithm>
#include <chrono>
#include <filesystem>
#include <iostream>
#include <thread>
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

using Clock = std::chrono::steady_clock;

double MsSince(Clock::time_point start) {
  return std::chrono::duration<double, std::milli>(Clock::now() - start).count();
}

// Hilos de trabajo: los nucleos menos el principal, entre 1 y 4.
std::size_t WorkerCount() {
  const unsigned n = std::thread::hardware_concurrency();
  return std::clamp<std::size_t>(n > 1 ? n - 1 : 1, 1, 4);
}

} // namespace

AssetManager::AssetManager(std::string rootPath)
    : root_(std::move(rootPath)), loader_(WorkerCount()) {
  if (!root_.empty() && root_.back() != '/' && root_.back() != '\\') {
    root_.push_back('/');
  }
}

std::string AssetManager::Resolve(const std::string& relativePath) const {
  return relativePath.empty() ? std::string{} : root_ + relativePath;
}

AssetManager::ModelJob AssetManager::LoadModelJob(const std::string& relativePath) const {
  namespace fs = std::filesystem;
  ModelJob job;
  job.obj = LoadObjData(Resolve(relativePath));
  const fs::path root = fs::path(root_).lexically_normal();
  for (const ObjData::Part& part : job.obj.parts) {
    if (part.texture.empty()) {
      job.textureKeys.emplace_back();
      job.images.push_back(Image{});
      continue;
    }
    // la clave de la cache es la ruta relativa a assets/ ("textures/mat_madera.png")
    job.textureKeys.push_back(fs::path(part.texture).lexically_relative(root).generic_string());
    job.images.push_back(LoadImage(part.texture.c_str())); // solo CPU: decodificar el PNG
  }
  return job;
}

std::shared_ptr<TextureResource> AssetManager::BuildTexture(const std::string& key, Image image) {
  if (auto it = textures_.find(key); it != textures_.end()) {
    UnloadImage(image); // ya estaba (la uso otro modelo antes)
    return it->second;
  }
  if (image.data == nullptr)
    throw std::runtime_error("No se pudo cargar la textura: " + key);
  auto tex = std::make_shared<TextureResource>(image);
  textures_.emplace(key, tex);
  return tex;
}

std::shared_ptr<ModelResource> AssetManager::BuildModel(const std::string& key, ModelJob job) {
  std::vector<std::shared_ptr<TextureResource>> textures;
  for (std::size_t i = 0; i < job.obj.parts.size(); ++i) {
    if (job.textureKeys[i].empty()) {
      textures.push_back(nullptr);
      continue;
    }
    try {
      textures.push_back(BuildTexture(job.textureKeys[i], job.images[i]));
    } catch (const std::exception& e) {
      std::cerr << "[modelo] " << key << ": " << e.what() << '\n';
      textures.push_back(nullptr); // se ve blanco, como hacia raylib
    }
  }
  auto model = std::make_shared<ModelResource>(job.obj, std::move(textures));
  models_.emplace(key, model);
  return model;
}

std::shared_ptr<TextureResource> AssetManager::GetTexture(const std::string& relativePath) {
  if (auto it = textures_.find(relativePath); it != textures_.end())
    return it->second;
  const auto start = Clock::now();
  std::shared_ptr<TextureResource> tex;
  if (auto it = pendingTextures_.find(relativePath); it != pendingTextures_.end()) {
    std::future<Image> f = std::move(it->second);
    pendingTextures_.erase(it);
    tex = BuildTexture(relativePath, f.get());
  } else {
    tex = std::make_shared<TextureResource>(Resolve(relativePath));
    textures_.emplace(relativePath, tex);
  }
  ++stats_.textures;
  stats_.textureMs += MsSince(start);
  return tex;
}

std::shared_ptr<ModelResource> AssetManager::GetModel(const std::string& relativePath) {
  if (auto it = models_.find(relativePath); it != models_.end())
    return it->second;
  const auto start = Clock::now();
  std::shared_ptr<ModelResource> model;
  if (auto it = pendingModels_.find(relativePath); it != pendingModels_.end()) {
    std::future<ModelJob> f = std::move(it->second);
    pendingModels_.erase(it);
    model = BuildModel(relativePath, f.get()); // espera si el hilo aun no ha terminado
  } else {
    model = BuildModel(relativePath, LoadModelJob(relativePath));
  }
  ++stats_.models;
  stats_.modelMs += MsSince(start);
  return model;
}

void AssetManager::RequestModel(const std::string& relativePath) {
  if (relativePath.empty() || models_.contains(relativePath) || pendingModels_.contains(relativePath))
    return;
  pendingModels_.emplace(relativePath,
                         loader_.Submit([this, relativePath] { return LoadModelJob(relativePath); }));
}

void AssetManager::RequestTexture(const std::string& relativePath) {
  if (relativePath.empty() || textures_.contains(relativePath) ||
      pendingTextures_.contains(relativePath))
    return;
  const std::string full = Resolve(relativePath);
  pendingTextures_.emplace(relativePath, loader_.Submit([full] {
                             Image img = LoadImage(full.c_str());
                             if (img.data == nullptr)
                               throw std::runtime_error("No se pudo cargar la textura: " + full);
                             return img;
                           }));
}

std::size_t AssetManager::PumpUploads(double budgetMs) {
  const auto start = Clock::now();
  auto ready = [](const auto& f) {
    return f.wait_for(std::chrono::seconds(0)) == std::future_status::ready;
  };
  for (auto it = pendingModels_.begin(); it != pendingModels_.end() && MsSince(start) < budgetMs;) {
    if (!ready(it->second)) {
      ++it;
      continue;
    }
    const std::string key = it->first;
    std::future<ModelJob> f = std::move(it->second);
    it = pendingModels_.erase(it);
    try {
      BuildModel(key, f.get());
    } catch (const std::exception& e) {
      std::cerr << "[precarga] no se pudo cargar " << key << ": " << e.what() << '\n';
    }
  }
  for (auto it = pendingTextures_.begin();
       it != pendingTextures_.end() && MsSince(start) < budgetMs;) {
    if (!ready(it->second)) {
      ++it;
      continue;
    }
    const std::string key = it->first;
    std::future<Image> f = std::move(it->second);
    it = pendingTextures_.erase(it);
    try {
      BuildTexture(key, f.get());
    } catch (const std::exception& e) {
      std::cerr << "[precarga] no se pudo cargar " << key << ": " << e.what() << '\n';
    }
  }
  for (auto it = pendingSounds_.begin(); it != pendingSounds_.end() && MsSince(start) < budgetMs;) {
    if (!ready(it->second)) {
      ++it;
      continue;
    }
    const std::string key = it->first;
    std::future<Wave> f = std::move(it->second);
    it = pendingSounds_.erase(it);
    try {
      sounds_.emplace(key, std::make_shared<SoundResource>(f.get()));
    } catch (const std::exception& e) {
      std::cerr << "[precarga] no se pudo cargar " << key << ": " << e.what() << '\n';
    }
  }
  return pendingModels_.size() + pendingTextures_.size() + pendingSounds_.size();
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
  if (auto it = pendingSounds_.find(relativePath); it != pendingSounds_.end()) {
    const auto start = Clock::now();
    std::future<Wave> f = std::move(it->second);
    pendingSounds_.erase(it);
    auto sound = std::make_shared<SoundResource>(f.get());
    sounds_.emplace(relativePath, sound);
    ++stats_.sounds;
    stats_.soundMs += MsSince(start);
    return sound;
  }
  return GetOrLoad(sounds_, {&stats_.sounds, &stats_.soundMs}, relativePath,
                   Resolve(relativePath));
}

void AssetManager::RequestSound(const std::string& relativePath) {
  if (relativePath.empty() || sounds_.contains(relativePath) || pendingSounds_.contains(relativePath))
    return;
  const std::string full = Resolve(relativePath);
  pendingSounds_.emplace(relativePath, loader_.Submit([full] {
                           Wave w = LoadWave(full.c_str()); // solo CPU: decodificar
                           if (w.data == nullptr)
                             throw std::runtime_error("No se pudo cargar el sonido: " + full);
                           return w;
                         }));
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