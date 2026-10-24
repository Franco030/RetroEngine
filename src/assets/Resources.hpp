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

// Caja de colision que el propio modelo trae en su .obj, en una linea de comentario:
//   # retro:col min_x min_y min_z max_x max_y max_z yaw
// (coordenadas del motor; las escribe tools/blender/retro_comun.py). tinyobj ignora los
// comentarios, asi que el modelo se sigue cargando igual en cualquier otro programa.
struct ModelBox {
  Vector3 min{};
  Vector3 max{};
  float yaw = 0.0f;
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

  // Materiales sin textura propia (en el .mtl no traen map_Kd, o el modelo no tiene .mtl): son
  // los que reciben la textura que la entidad indica con "texture". Los que si traen la suya
  // (madera, tejas...) la conservan.
  const std::vector<int>& OpenMaterials() const noexcept { return openMaterials_; }

  // Cajas de colision del .obj (vacio si no trae ninguna: se usa la caja de todo el modelo).
  const std::vector<ModelBox>& CollisionBoxes() const noexcept { return collisionBoxes_; }

private:
  void Release() noexcept;
  void CollectOwnedTextures();

  Model model_{};
  std::vector<Texture2D> ownedTextures_;
  std::vector<int> openMaterials_;
  std::vector<ModelBox> collisionBoxes_;
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

class FontResource {
public:
  FontResource(const std::string& path, int baseSize);
  ~FontResource();

  FontResource(const FontResource&) = delete;
  FontResource& operator=(const FontResource&) = delete;
  FontResource(FontResource&& other) noexcept;
  FontResource& operator=(FontResource&& other) noexcept;

  const Font& Get() const noexcept { return font_; }
  bool IsValid() const noexcept { return font_.texture.id != 0; }

private:
  void Release() noexcept;

  Font font_{};
};

class SoundResource {
public:
  explicit SoundResource(const std::string& path);
  ~SoundResource();

  SoundResource(const SoundResource&) = delete;
  SoundResource& operator=(const SoundResource&) = delete;
  SoundResource(SoundResource&& other) noexcept;
  SoundResource& operator=(SoundResource&& other) noexcept;

  const Sound& Get() const noexcept { return sound_; }
  bool IsValid() const noexcept { return sound_.frameCount > 0; }

private:
  void Release() noexcept;
  Sound sound_{};
};

class MusicResource {
public:
  explicit MusicResource(const std::string& path);
  ~MusicResource();

  MusicResource(const MusicResource&) = delete;
  MusicResource& operator=(const MusicResource&) = delete;
  MusicResource(MusicResource&& other) noexcept;
  MusicResource& operator=(MusicResource&& other) noexcept;

  Music& Get() noexcept { return music_; }
  bool IsValid() const noexcept { return music_.frameCount > 0; }

private:
  void Release() noexcept;
  Music music_{};
};

} // namespace retro