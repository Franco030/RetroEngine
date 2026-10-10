#include "assets/Resources.hpp"

#include <raylib.h>
#include <rlgl.h>

#include <algorithm>
#include <stdexcept>
#include <utility>

namespace retro {

namespace {

constexpr int MAX_MATERIAL_MAPS = 12;
const char* ToCStr(const std::string& s) { return s.empty() ? nullptr : s.c_str(); }

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
ModelResource::ModelResource(const std::string& path) : model_(::LoadModel(path.c_str())) {
  CollectOwnedTextures();

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
      shader_(std::move(other.shader_)) {
  other.ownedTextures_.clear();
}

ModelResource& ModelResource::operator=(ModelResource&& other) noexcept {
  if (this != &other) {
    Release();
    model_ = std::exchange(other.model_, Model{});
    ownedTextures_ = std::move(other.ownedTextures_);
    shader_ = std::move(other.shader_);
    other.ownedTextures_.clear();
  }

  return *this;
}

void ModelResource::CollectOwnedTextures() {
  const unsigned int defaultId = rlGetTextureIdDefault();

  for (int m = 0; m < model_.materialCount; ++m) {
    if (model_.materials[m].maps == nullptr)
      continue;

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
ShaderResource::ShaderResource(const std::string& vertexPath, const std::string& fragmentPath)
    : shader_(::LoadShader(ToCStr(vertexPath), ToCStr(fragmentPath))) {

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

} // namespace retro