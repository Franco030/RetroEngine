#include "assets/Resources.hpp"

#include <raylib.h>
#include <rlgl.h>

#include <algorithm>
#include <cctype>
#include <fstream>
#include <initializer_list>
#include <sstream>
#include <stdexcept>
#include <string>
#include <utility>

namespace retro {

namespace {

constexpr int MAX_MATERIAL_MAPS = 12;
const char* ToCStr(const std::string& s) { return s.empty() ? nullptr : s.c_str(); }

// El lector de OBJ de raylib (tinyobj) ABORTA el programa con caras de mas de 5 vertices (un
// assert, no un error). Se comprueba antes, para que sea una excepcion normal con su mensaje.
constexpr int kMaxObjFaceVerts = 5;

void CheckObjFaces(const std::string& path) {
  std::string ext = path.size() >= 4 ? path.substr(path.size() - 4) : std::string();
  std::transform(ext.begin(), ext.end(), ext.begin(),
                 [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
  if (ext != ".obj")
    return;

  std::ifstream in(path);
  std::string line;
  int number = 0;
  while (std::getline(in, line)) {
    ++number;
    if (line.size() < 2 || line[0] != 'f' || (line[1] != ' ' && line[1] != '\t'))
      continue;
    int verts = 0;
    bool inToken = false;
    for (std::size_t i = 1; i < line.size(); ++i) {
      const bool space = std::isspace(static_cast<unsigned char>(line[i])) != 0;
      if (!space && !inToken)
        ++verts;
      inToken = !space;
    }
    if (verts > kMaxObjFaceVerts) {
      throw std::runtime_error(path + ":" + std::to_string(number) + ": cara de " +
                               std::to_string(verts) + " vertices (maximo " +
                               std::to_string(kMaxObjFaceVerts) +
                               "); exporta el modelo triangulado");
    }
  }
}

// Lee las lineas "# retro:col ..." del .obj (ver ModelBox en Resources.hpp).
std::vector<ModelBox> ReadObjBoxes(const std::string& path) {
  std::vector<ModelBox> boxes;
  std::ifstream in(path);
  std::string line;
  int number = 0;
  const std::string tag = "# retro:col ";
  while (std::getline(in, line)) {
    ++number;
    if (line.rfind(tag, 0) != 0)
      continue;
    std::istringstream ss(line.substr(tag.size()));
    ModelBox b;
    if (!(ss >> b.min.x >> b.min.y >> b.min.z >> b.max.x >> b.max.y >> b.max.z >> b.yaw) ||
        b.min.x >= b.max.x || b.min.y >= b.max.y || b.min.z >= b.max.z) {
      throw std::runtime_error(path + ":" + std::to_string(number) +
                               ": caja de colision mal escrita (min x y z, max x y z, yaw)");
    }
    boxes.push_back(b);
  }
  return boxes;
}

Model LoadCheckedModel(const std::string& path) {
  CheckObjFaces(path);
  return ::LoadModel(path.c_str());
}

} // namespace

// ---------------------------------------------------------------------------
// TextureResource
// ---------------------------------------------------------------------------
TextureResource::TextureResource(const std::string& path) : texture_(::LoadTexture(path.c_str())) {
  if (!IsTextureValid(texture_)) {
    throw std::runtime_error("No se pudo cargar la textura: " + path);
  }

  SetTextureFilter(texture_, TEXTURE_FILTER_POINT);
}

TextureResource::~TextureResource() { Release(); }

void TextureResource::SetWrap(int wrap) noexcept { ::SetTextureWrap(texture_, wrap); }

TextureResource::TextureResource(TextureResource&& other) noexcept
    : texture_(std::exchange(other.texture_, Texture2D{})) {}

TextureResource& TextureResource::operator=(TextureResource&& other) noexcept {
  if (this != &other) {
    Release();
    texture_ = std::exchange(other.texture_, Texture2D{});
  }

  return *this;
}

void TextureResource::Release() noexcept {
  if (texture_.id != 0) {
    UnloadTexture(texture_);
    texture_ = Texture2D{};
  }
}

// ---------------------------------------------------------------------------
// ModelResource
// ---------------------------------------------------------------------------
ModelResource::ModelResource(const std::string& path) : model_(LoadCheckedModel(path)) {
  CollectOwnedTextures();
  try {
    collisionBoxes_ = ReadObjBoxes(path);
  } catch (...) {
    Release();
    throw;
  }

  if (!IsModelValid(model_)) {
    Release();
    throw std::runtime_error("No se pudo cargar el modelo: " + path);
  }

  for (const Texture2D& tex : ownedTextures_) {
    SetTextureFilter(tex, TEXTURE_FILTER_POINT);
  }
}

ModelResource::ModelResource(Mesh mesh) : model_(::LoadModelFromMesh(mesh)) {
  CollectOwnedTextures();
  if (!IsModelValid(model_)) {
    Release();
    throw std::runtime_error("No se pudo crear el modelo desde la malla");
  }
}

ModelResource::~ModelResource() { Release(); }

void ModelResource::SetShader(std::shared_ptr<ShaderResource> shader) {
  if (!shader || !shader->IsValid()) {
    throw std::invalid_argument("ModelResource::SetShader: shader nulo o invalido");
  }

  for (int m = 0; m < model_.materialCount; ++m) {
    model_.materials[m].shader = shader->Get();
  }

  shader_ = std::move(shader);
}

ModelResource::ModelResource(ModelResource&& other) noexcept
    : model_(std::exchange(other.model_, Model{})), ownedTextures_(std::move(other.ownedTextures_)),
      openMaterials_(std::move(other.openMaterials_)),
      collisionBoxes_(std::move(other.collisionBoxes_)), shader_(std::move(other.shader_)) {
  other.ownedTextures_.clear();
  other.openMaterials_.clear();
}

ModelResource& ModelResource::operator=(ModelResource&& other) noexcept {
  if (this != &other) {
    Release();
    model_ = std::exchange(other.model_, Model{});
    ownedTextures_ = std::move(other.ownedTextures_);
    openMaterials_ = std::move(other.openMaterials_);
    collisionBoxes_ = std::move(other.collisionBoxes_);
    shader_ = std::move(other.shader_);
    other.ownedTextures_.clear();
    other.openMaterials_.clear();
  }

  return *this;
}

void ModelResource::CollectOwnedTextures() {
  const unsigned int defaultId = rlGetTextureIdDefault();

  for (int m = 0; m < model_.materialCount; ++m) {
    if (model_.materials[m].maps == nullptr)
      continue;

    const unsigned int diffuse = model_.materials[m].maps[MATERIAL_MAP_DIFFUSE].texture.id;
    if (diffuse == 0 || diffuse == defaultId)
      openMaterials_.push_back(m);

    for (int k = 0; k < MAX_MATERIAL_MAPS; ++k) {
      const Texture2D tex = model_.materials[m].maps[k].texture;
      if (tex.id == 0 || tex.id == defaultId)
        continue;

      const bool alreadyTracked = std::any_of(ownedTextures_.begin(), ownedTextures_.end(),
                                              [&](const Texture2D& t) { return t.id == tex.id; });

      if (!alreadyTracked)
        ownedTextures_.push_back(tex);
    }
  }
}

void ModelResource::Release() noexcept {
  for (const Texture2D& tex : ownedTextures_) {
    UnloadTexture(tex);
  }
  ownedTextures_.clear();

  if (model_.meshCount > 0 || model_.materials != nullptr) {
    UnloadModel(model_);
  }
  model_ = Model{};
  shader_.reset();
}

// ---------------------------------------------------------------------------
// ShaderResource
// ---------------------------------------------------------------------------
ShaderResource::ShaderResource(const std::string& vertexPath, const std::string& fragmentPath) {
  // Con una ruta inexistente Raylib usa su shader por defecto sin dar error: se comprueba antes.
  for (const std::string* path : {&vertexPath, &fragmentPath}) {
    if (!path->empty() && !FileExists(path->c_str()))
      throw std::runtime_error("Shader inexistente: " + *path);
  }

  shader_ = ::LoadShader(ToCStr(vertexPath), ToCStr(fragmentPath));
  if (!IsShaderValid(shader_)) {
    Release();
    throw std::runtime_error("No se pudo cargar el shader: [" + vertexPath + "] [" + fragmentPath +
                             "]");
  }
}

ShaderResource::~ShaderResource() { Release(); }

ShaderResource::ShaderResource(ShaderResource&& other) noexcept
    : shader_(std::exchange(other.shader_, Shader{})) {}

ShaderResource& ShaderResource::operator=(ShaderResource&& other) noexcept {
  if (this != &other) {
    Release();
    shader_ = std::exchange(other.shader_, Shader{});
  }

  return *this;
}

void ShaderResource::Release() noexcept {
  if (shader_.id != 0 || shader_.locs != nullptr) {
    UnloadShader(shader_);
  }
  shader_ = Shader{};
}

// ---------------------------------------------------------------------------
// FontResource
// ---------------------------------------------------------------------------
FontResource::FontResource(const std::string& path, int baseSize) {
  if (!FileExists(path.c_str())) {
    throw std::runtime_error("Fuente inexistente: " + path);
  }

  int codepoints[224];
  for (int i = 0; i < 224; ++i)
    codepoints[i] = 32 + i;

  font_ = ::LoadFontEx(path.c_str(), baseSize, codepoints, 224);
  if (!IsFontValid(font_) || font_.texture.id == GetFontDefault().texture.id) {
    font_ = Font{};
    throw std::runtime_error("No se pudo cargar la fuente: " + path);
  }

  SetTextureFilter(font_.texture, TEXTURE_FILTER_BILINEAR);
}

FontResource::~FontResource() { Release(); }

FontResource::FontResource(FontResource&& other) noexcept
    : font_(std::exchange(other.font_, Font{})) {}

FontResource& FontResource::operator=(FontResource&& other) noexcept {
  if (this != &other) {
    Release();
    font_ = std::exchange(other.font_, Font{});
  }

  return *this;
}

void FontResource::Release() noexcept {
  if (font_.texture.id != 0)
    UnloadFont(font_);
  font_ = Font{};
}

// ---------------------------------------------------------------------------
// SoundResource
// ---------------------------------------------------------------------------
SoundResource::SoundResource(const std::string& path) {
  if (!FileExists(path.c_str()))
    throw std::runtime_error("Sonido inexistente: " + path);
  sound_ = ::LoadSound(path.c_str());
  if (!IsSoundValid(sound_)) {
    sound_ = Sound{};
    throw std::runtime_error("No se pudo cargar el sonido: " + path);
  }
}

SoundResource::~SoundResource() { Release(); }

SoundResource::SoundResource(SoundResource&& other) noexcept
    : sound_(std::exchange(other.sound_, Sound{})) {}

SoundResource& SoundResource::operator=(SoundResource&& other) noexcept {
  if (this != &other) {
    Release();
    sound_ = std::exchange(other.sound_, Sound{});
  }

  return *this;
}

void SoundResource::Release() noexcept {
  if (sound_.frameCount > 0)
    UnloadSound(sound_);
  sound_ = Sound{};
}

// ---------------------------------------------------------------------------
// MusicResource
// ---------------------------------------------------------------------------

MusicResource::MusicResource(const std::string& path) {
  if (!FileExists(path.c_str()))
    throw std::runtime_error("Musica inexistente: " + path);
  music_ = ::LoadMusicStream(path.c_str());
  if (!IsMusicValid(music_)) {
    music_ = Music{};
    throw std::runtime_error("No se pudo cargar la musica: " + path);
  }
  music_.looping = true;
}

MusicResource::~MusicResource() { Release(); }

MusicResource::MusicResource(MusicResource&& other) noexcept
    : music_(std::exchange(other.music_, Music{})) {}

MusicResource& MusicResource::operator=(MusicResource&& other) noexcept {
  if (this != &other) {
    Release();
    music_ = std::exchange(other.music_, Music{});
  }

  return *this;
}

void MusicResource::Release() noexcept {
  if (music_.frameCount > 0)
    UnloadMusicStream(music_);
  music_ = Music{};
}

} // namespace retro