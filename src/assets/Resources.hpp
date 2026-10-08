#pragma once

#include <raylib.h>

#include <memory>
#include <string>
#include <vector>

namespace retro {

class ShaderResource;

class TextureResource {
public:
  explicit TextureResource(const std::string& path);
  ~TextureResource();

  TextureResource(const TextureResource&) = delete;
  TextureResource& operator=(const TextureResource&) = delete;
  TextureResource(TextureResource&& other) noexcept;
  TextureResource& operator=(TextureResource&& other) noexcept;

  const Texture2D& Get() const noexcept { return texture_; }
  bool IsValid() const noexcept { return texture_.id != 0; }
  void SetWrap(int wrap) noexcept;

private:
  void Release() noexcept;
  Texture2D texture_{};
};

class ModelResource {
public:
  explicit ModelResource(const std::string& path);
  explicit ModelResource(Mesh mesh);
  ~ModelResource();

  ModelResource(const ModelResource&) = delete;
  ModelResource& operator=(const ModelResource&) = delete;
  ModelResource(ModelResource&& other) noexcept;
  ModelResource& operator=(ModelResource&& other) noexcept;

  Model& Get() noexcept { return model_; }
  const Model& Get() const noexcept { return model_; }
  bool IsValid() const noexcept { return model_.meshCount > 0; }

  void SetShader(std::shared_ptr<ShaderResource> shader);

private:
  void Release() noexcept;
  void CollectOwnedTextures();

  Model model_{};
  std::vector<Texture2D> ownedTextures_;
  std::shared_ptr<ShaderResource> shader_;
};

class ShaderResource {
public:
  ShaderResource(const std::string& vertexPath, const std::string& fragmentPath);
  ~ShaderResource();

  ShaderResource(const ShaderResource&) = delete;
  ShaderResource& operator=(const ShaderResource&) = delete;
  ShaderResource(ShaderResource&& other) noexcept;
  ShaderResource& operator=(ShaderResource&& other) noexcept;

  const Shader& Get() const noexcept { return shader_; }
  bool IsValid() const noexcept { return shader_.id != 0; }

private:
  void Release() noexcept;
  Shader shader_{};
};

} // namespace retro