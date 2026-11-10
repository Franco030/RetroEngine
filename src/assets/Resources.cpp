#include "assets/Resources.hpp"

#include <raylib.h>
#include <raymath.h>
#include <rlgl.h>

#include <algorithm>
#include <cctype>
#include <cstring>
#include <fstream>
#include <initializer_list>
#include <sstream>
#include <stdexcept>
#include <string>
#include <utility>

namespace retro {

namespace {

const char* ToCStr(const std::string& s) { return s.empty() ? nullptr : s.c_str(); }

// Copia un vector a memoria de raylib (UnloadMesh la libera con RL_FREE).
template <class T> T* RaylibCopy(const std::vector<T>& v) {
  T* out = static_cast<T*>(MemAlloc(static_cast<unsigned int>(v.size() * sizeof(T))));
  std::copy(v.begin(), v.end(), out);
  return out;
}

int FindGlyph(const Font& font, int codepoint) {
  for (int i = 0; i < font.glyphCount; ++i) {
    if (font.glyphs[i].value == codepoint)
      return i;
  }
  return -1;
}

// La cobertura de un glifo como imagen de 1 byte por pixel (lo que espera GenImageFontAtlas). Al
// cargar, raylib deja cada glifo como un trozo del atlas en gris+alfa: la forma va en el alfa.
Image GlyphCoverage(const Image& im) {
  const int n = im.width * im.height;
  auto* out = static_cast<unsigned char*>(MemAlloc(static_cast<unsigned int>(std::max(n, 1))));
  const auto* p = static_cast<const unsigned char*>(im.data);
  for (int i = 0; i < n; ++i) {
    if (p == nullptr)
      out[i] = 0;
    else if (im.format == PIXELFORMAT_UNCOMPRESSED_GRAY_ALPHA)
      out[i] = p[2 * i + 1];
    else if (im.format == PIXELFORMAT_UNCOMPRESSED_GRAYSCALE)
      out[i] = p[i];
    else // otro formato: no se sabe leer, se trata como vacio
      out[i] = 0;
  }
  return Image{out, im.width, im.height, 1, PIXELFORMAT_UNCOMPRESSED_GRAYSCALE};
}

// La fuente pixel art no trae la eñe ni las vocales con tilde: se componen con la letra base y el
// acento dibujado encima, en bloques del tamaño de los "píxeles" de la fuente (el ancho del
// punto), y se rehace el atlas. Las letras que la fuente ya dibuje no se tocan (se piden del 32
// al 255, asi que las que no tiene estan, pero vacias).
void ComposeSpanishGlyphs(Font& font) {
  struct Composed {
    int codepoint;
    int base;
    char mark; // '~' virgulilla, '\'' tilde, ':' dieresis
  };
  static constexpr Composed kComposed[] = {
      {0xF1, 'n', '~'},  {0xD1, 'N', '~'},  {0xE1, 'a', '\''}, {0xE9, 'e', '\''},
      {0xED, 'i', '\''}, {0xF3, 'o', '\''}, {0xFA, 'u', '\''}, {0xC1, 'A', '\''},
      {0xC9, 'E', '\''}, {0xCD, 'I', '\''}, {0xD3, 'O', '\''}, {0xDA, 'U', '\''},
      {0xFC, 'u', ':'},  {0xDC, 'U', ':'}};

  const int dot = FindGlyph(font, '.');
  if (dot < 0)
    return;
  const int u = std::max(1, font.glyphs[dot].image.width); // un "pixel" de la fuente

  // Todos los glifos en 1 byte (los originales, copiados; los compuestos, nuevos).
  std::vector<GlyphInfo> glyphs(font.glyphs, font.glyphs + font.glyphCount);
  for (GlyphInfo& g : glyphs)
    g.image = GlyphCoverage(g.image);
  auto drawn = [&](int i) {
    const Image& im = glyphs[static_cast<std::size_t>(i)].image;
    const auto* p = static_cast<const unsigned char*>(im.data);
    return std::any_of(p, p + im.width * im.height, [](unsigned char v) { return v != 0; });
  };

  bool changed = false;
  for (const Composed& c : kComposed) {
    const int b = FindGlyph(font, c.base);
    const int existing = FindGlyph(font, c.codepoint);
    if (b < 0 || (existing >= 0 && drawn(existing)))
      continue;
    const GlyphInfo& base = glyphs[static_cast<std::size_t>(b)];
    const int w = base.image.width;
    const int h = base.image.height + 3 * u; // dos filas de acento y una de aire
    auto* px = static_cast<unsigned char*>(MemAlloc(static_cast<unsigned int>(w * h)));
    std::memset(px, 0, static_cast<std::size_t>(w * h));
    const auto* src = static_cast<const unsigned char*>(base.image.data);
    for (int y = 0; y < base.image.height; ++y)
      std::memcpy(px + (y + 3 * u) * w, src + y * w, static_cast<std::size_t>(w));

    const int cols = std::max(2, w / u);
    auto block = [&](int col, int row) { // un bloque de u x u en la rejilla del acento
      for (int y = row * u; y < (row + 1) * u; ++y)
        for (int x = col * u; x < std::min(w, (col + 1) * u); ++x)
          px[y * w + x] = 255;
    };
    if (c.mark == '~') {
      for (int col = 0; col < cols; ++col)
        block(col, (col == 0 || (col > cols / 2 && col < cols - 1)) ? 1 : 0);
    } else if (c.mark == '\'') {
      block(std::max(0, cols / 2 - 1), 1);
      block(std::min(cols - 1, cols / 2), 0);
    } else {
      block(std::max(0, cols / 2 - 2 + (cols < 4)), 1);
      block(std::min(cols - 1, cols / 2 + 1), 1);
    }

    GlyphInfo g = base;
    g.value = c.codepoint;
    g.offsetY = base.offsetY - 3 * u;
    g.image = Image{px, w, h, 1, PIXELFORMAT_UNCOMPRESSED_GRAYSCALE};
    if (existing >= 0) { // el hueco vacio: se cambia por el compuesto
      UnloadImage(glyphs[static_cast<std::size_t>(existing)].image);
      glyphs[static_cast<std::size_t>(existing)] = g;
    } else {
      glyphs.push_back(g);
    }
    changed = true;
  }
  if (!changed) {
    for (GlyphInfo& g : glyphs)
      UnloadImage(g.image);
    return;
  }

  // Atlas nuevo; despues, como hace raylib al cargar, cada glifo se queda con su trozo del atlas.
  // Las listas van en memoria de raylib: UnloadFont las libera.
  Rectangle* recs = nullptr;
  Image atlas = GenImageFontAtlas(glyphs.data(), &recs, static_cast<int>(glyphs.size()), font.baseSize,
                                  font.glyphPadding, 0);
  for (std::size_t i = 0; i < glyphs.size(); ++i) {
    UnloadImage(glyphs[i].image);
    glyphs[i].image = ImageFromImage(atlas, recs[i]);
  }
  for (int i = 0; i < font.glyphCount; ++i)
    UnloadImage(font.glyphs[i].image);
  MemFree(font.glyphs);
  MemFree(font.recs);
  UnloadTexture(font.texture);
  font.glyphs = RaylibCopy(glyphs);
  font.recs = recs;
  font.glyphCount = static_cast<int>(glyphs.size());
  font.texture = LoadTextureFromImage(atlas);
  UnloadImage(atlas);
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

TextureResource::TextureResource(Image image) : texture_(::LoadTextureFromImage(image)) {
  UnloadImage(image);
  if (!IsTextureValid(texture_))
    throw std::runtime_error("No se pudo subir la textura");
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
ModelResource::ModelResource(const ObjData& data,
                             std::vector<std::shared_ptr<TextureResource>> textures)
    : textures_(std::move(textures)), collisionBoxes_(data.boxes) {
  const int n = static_cast<int>(data.parts.size());
  model_.transform = MatrixIdentity();
  model_.meshCount = n;
  model_.materialCount = n;
  model_.meshes = static_cast<Mesh*>(MemAlloc(sizeof(Mesh) * static_cast<unsigned int>(n)));
  model_.materials = static_cast<Material*>(MemAlloc(sizeof(Material) * static_cast<unsigned int>(n)));
  model_.meshMaterial = static_cast<int*>(MemAlloc(sizeof(int) * static_cast<unsigned int>(n)));
  for (int i = 0; i < n; ++i) {
    const ObjData::Part& part = data.parts[static_cast<std::size_t>(i)];
    Mesh& mesh = model_.meshes[i];
    mesh.vertexCount = static_cast<int>(part.positions.size() / 3);
    mesh.triangleCount = mesh.vertexCount / 3;
    mesh.vertices = RaylibCopy(part.positions);
    mesh.texcoords = RaylibCopy(part.texcoords);
    mesh.normals = RaylibCopy(part.normals);
    UploadMesh(&mesh, false);

    model_.materials[i] = LoadMaterialDefault();
    model_.materials[i].maps[MATERIAL_MAP_DIFFUSE].color = part.color;
    const std::size_t k = static_cast<std::size_t>(i);
    if (k < textures_.size() && textures_[k])
      model_.materials[i].maps[MATERIAL_MAP_DIFFUSE].texture = textures_[k]->Get();
    model_.meshMaterial[i] = i;
  }
  CollectOpenMaterials();
}

ModelResource::ModelResource(Mesh mesh) : model_(::LoadModelFromMesh(mesh)) {
  CollectOpenMaterials();
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
    : model_(std::exchange(other.model_, Model{})), textures_(std::move(other.textures_)),
      openMaterials_(std::move(other.openMaterials_)),
      collisionBoxes_(std::move(other.collisionBoxes_)), shader_(std::move(other.shader_)) {
  other.textures_.clear();
  other.openMaterials_.clear();
}

ModelResource& ModelResource::operator=(ModelResource&& other) noexcept {
  if (this != &other) {
    Release();
    model_ = std::exchange(other.model_, Model{});
    textures_ = std::move(other.textures_);
    openMaterials_ = std::move(other.openMaterials_);
    collisionBoxes_ = std::move(other.collisionBoxes_);
    shader_ = std::move(other.shader_);
    other.textures_.clear();
    other.openMaterials_.clear();
  }

  return *this;
}

// Materiales sin textura: los que reciben la de la entidad ("texture").
void ModelResource::CollectOpenMaterials() {
  const unsigned int defaultId = rlGetTextureIdDefault();
  for (int m = 0; m < model_.materialCount; ++m) {
    if (model_.materials[m].maps == nullptr)
      continue;
    const unsigned int diffuse = model_.materials[m].maps[MATERIAL_MAP_DIFFUSE].texture.id;
    if (diffuse == 0 || diffuse == defaultId)
      openMaterials_.push_back(m);
  }
}

void ModelResource::Release() noexcept {
  // Las texturas son de la cache (textures_): UnloadModel no las toca.
  if (model_.meshCount > 0 || model_.materials != nullptr) {
    UnloadModel(model_);
  }
  model_ = Model{};
  textures_.clear();
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
  ComposeSpanishGlyphs(font_);

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

SoundResource::SoundResource(Wave wave) : sound_(::LoadSoundFromWave(wave)) {
  UnloadWave(wave);
  if (!IsSoundValid(sound_)) {
    sound_ = Sound{};
    throw std::runtime_error("No se pudo crear el sonido");
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