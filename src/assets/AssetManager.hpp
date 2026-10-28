#pragma once

#include "assets/AsyncLoader.hpp"
#include "assets/Resources.hpp"

#include <future>
#include <memory>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

namespace retro {

class AssetManager {
public:
  explicit AssetManager(std::string rootPath = "assets/");
  ~AssetManager() = default;

  AssetManager(const AssetManager&) = delete;
  AssetManager& operator=(const AssetManager&) = delete;
  AssetManager(AssetManager&&) = delete;
  AssetManager& operator=(AssetManager&&) = delete;

  // Lo devuelven ya cargado. Si estaba pedido a los hilos (Request*), espera a que terminen;
  // si no, lo lee aqui mismo.
  std::shared_ptr<TextureResource> GetTexture(const std::string& relativePath);
  std::shared_ptr<ModelResource> GetModel(const std::string& relativePath);

  // Carga en dos fases. Request* manda leer y parsear el archivo a los hilos de trabajo (no
  // bloquea); PumpUploads, en el hilo principal, sube a la GPU lo que ya este listo hasta
  // gastar 'budgetMs' y devuelve cuantos quedan por terminar. Pedir algo ya cargado o ya pedido
  // no hace nada.
  void RequestModel(const std::string& relativePath);
  void RequestTexture(const std::string& relativePath);
  void RequestSound(const std::string& relativePath);
  std::size_t PumpUploads(double budgetMs);
  bool IsPending(const std::string& relativePath) const {
    return pendingModels_.contains(relativePath) || pendingTextures_.contains(relativePath) ||
           pendingSounds_.contains(relativePath);
  }

  // Para otras tareas sin GPU (parsear el JSON de una escena).
  AsyncLoader& Loader() noexcept { return loader_; }
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
  bool HasSound(const std::string& relativePath) const { return sounds_.contains(relativePath); }

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
  // Lo que un hilo prepara para un modelo: el .obj leido y, por cada parte, la clave de su
  // textura en la cache y la imagen ya decodificada (por si aun no esta cargada).
  struct ModelJob {
    ObjData obj;
    std::vector<std::string> textureKeys;
    std::vector<Image> images;
  };

  std::string Resolve(const std::string& relativePath) const;
  ModelJob LoadModelJob(const std::string& relativePath) const; // sin GPU: vale en cualquier hilo
  std::shared_ptr<ModelResource> BuildModel(const std::string& key, ModelJob job);
  std::shared_ptr<TextureResource> BuildTexture(const std::string& key, Image image);

  std::string root_;
  std::unordered_map<std::string, std::shared_ptr<TextureResource>> textures_;
  std::unordered_map<std::string, std::shared_ptr<ModelResource>> models_;
  std::unordered_map<std::string, std::shared_ptr<ShaderResource>> shaders_;
  std::unordered_map<std::string, std::shared_ptr<FontResource>> fonts_;
  std::unordered_map<std::string, std::shared_ptr<SoundResource>> sounds_;
  LoadStats stats_;

  // Ultimo: los hilos se paran (y se descartan las tareas pendientes) antes que lo demas.
  std::unordered_map<std::string, std::future<ModelJob>> pendingModels_;
  std::unordered_map<std::string, std::future<Image>> pendingTextures_;
  std::unordered_map<std::string, std::future<Wave>> pendingSounds_;
  AsyncLoader loader_;
};

} // namespace retro