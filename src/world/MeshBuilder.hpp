#pragma once

#include "assets/ObjLoader.hpp"

#include <raylib.h>
#include <raymath.h>

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdint>
#include <string>
#include <utility>

// Lo comun de la geometria que se genera en codigo (la ciudad del cilindro, los interiores de sus
// torres): azar con semilla, un armador de mallas por textura y letras de una rejilla.
namespace retro::gen {

// --------------------------------------------------------------------------- azar con semilla
// Cada trozo sale de su numero: la misma semilla, la misma ciudad (splitmix64).
inline std::uint64_t Mix(std::uint64_t x) {
  x += 0x9E3779B97F4A7C15ull;
  x = (x ^ (x >> 30)) * 0xBF58476D1CE4E5B9ull;
  x = (x ^ (x >> 27)) * 0x94D049BB133111EBull;
  return x ^ (x >> 31);
}

class Rng {
public:
  explicit Rng(std::uint64_t seed) : s_(Mix(seed)) {}
  std::uint64_t Next() { return s_ = Mix(s_); }
  float Unit() { return static_cast<float>(Next() >> 40) / static_cast<float>(1ull << 24); }
  float Range(float a, float b) { return a + (b - a) * Unit(); }
  bool Chance(float p) { return Unit() < p; }
  int Pick(int n) { return static_cast<int>(Next() % static_cast<std::uint64_t>(std::max(1, n))); }
  // Para sacar otra serie de esta sin gastarle numeros (lo de cerca no cambia lo de lejos).
  std::uint64_t State() const { return s_; }

private:
  std::uint64_t s_;
};

inline std::uint64_t Seed(std::uint64_t base, long long chunk, int a, int b = 0, int c = 0) {
  std::uint64_t h = Mix(base ^ static_cast<std::uint64_t>(chunk) * 0xD6E8FEB86659FD93ull);
  h = Mix(h ^ (static_cast<std::uint64_t>(a) << 1));
  h = Mix(h ^ (static_cast<std::uint64_t>(b) << 17));
  return Mix(h ^ (static_cast<std::uint64_t>(c) << 33));
}

// --------------------------------------------------------------------------- geometria
// Arma un ObjData parte a parte (una por textura). Las uv se dan con v hacia arriba en la imagen,
// como en un .obj; aqui se invierten, como hace el lector de .obj para raylib.
class Builder {
public:
  explicit Builder(ObjData& data) : data_(data) {}

  ObjData::Part& Part(const std::string& texture) {
    for (ObjData::Part& p : data_.parts) {
      if (p.texture == texture)
        return p;
    }
    ObjData::Part p;
    p.material = texture;
    p.texture = texture;
    data_.parts.push_back(std::move(p));
    return data_.parts.back();
  }

  // 'n': hacia donde debe mirar la cara (si las esquinas vienen al reves, se dan la vuelta).
  void Quad(const std::string& tex, Vector3 a, Vector3 b, Vector3 c, Vector3 d, Vector2 ua, Vector2 ub,
            Vector2 uc, Vector2 ud, Vector3 n) {
    if (tex.empty())
      return;
    if (Vector3DotProduct(Vector3CrossProduct(Vector3Subtract(b, a), Vector3Subtract(c, a)), n) < 0.0f) {
      std::swap(b, d);
      std::swap(ub, ud);
    }
    ObjData::Part& p = Part(tex);
    auto push = [&](Vector3 v, Vector2 t) {
      p.positions.insert(p.positions.end(), {v.x, v.y, v.z});
      p.texcoords.insert(p.texcoords.end(), {t.x, 1.0f - t.y});
      p.normals.insert(p.normals.end(), {n.x, n.y, n.z});
    };
    push(a, ua);
    push(b, ub);
    push(c, uc);
    push(a, ua);
    push(c, uc);
    push(d, ud);
  }

  // Una caja que crece desde 'base' hacia 'up', de h0 a h1: los lados con 'side' (uv: 'tile' m por
  // vuelta de textura, desplazadas 'off') partidos en tramos de 'seg' m de alto (el mapeo afin
  // de la PS1 deforma mucho los triangulos grandes), y la tapa con 'top' (si hay).
  void Box(const std::string& side, const std::string& top, Vector3 base, Vector3 up, Vector3 across,
           Vector3 along, float w, float l, float h0, float h1, Vector2 tile, Vector2 off, float seg) {
    const float hw = w * 0.5f, hl = l * 0.5f;
    auto p = [&](float a, float b, float h) {
      return Vector3Add(base, Vector3Add(Vector3Scale(across, a), Vector3Add(Vector3Scale(along, b), Vector3Scale(up, h))));
    };
    const struct {
      float a0, b0, a1, b1;
      Vector3 n;
      float span;
    } faces[4] = {{-hw, -hl, hw, -hl, Vector3Negate(along), w},
                  {hw, -hl, hw, hl, across, l},
                  {hw, hl, -hw, hl, along, w},
                  {-hw, hl, -hw, -hl, Vector3Negate(across), l}};
    const int pieces = std::max(1, static_cast<int>(std::ceil((h1 - h0) / std::max(seg, 1.0f))));
    for (const auto& f : faces) {
      // a lo ancho tambien, en tramos de 'seg' (las caras de una torre miden hasta 90 m)
      const int across_n = std::max(1, static_cast<int>(std::ceil(f.span / std::max(seg, 1.0f))));
      for (int j = 0; j < across_n; ++j) {
        const float ta = static_cast<float>(j) / across_n, tb = static_cast<float>(j + 1) / across_n;
        const float a0 = f.a0 + (f.a1 - f.a0) * ta, a1 = f.a0 + (f.a1 - f.a0) * tb;
        const float b0 = f.b0 + (f.b1 - f.b0) * ta, b1 = f.b0 + (f.b1 - f.b0) * tb;
        const float u0 = off.x + f.span * ta / tile.x, u1 = off.x + f.span * tb / tile.x;
        for (int k = 0; k < pieces; ++k) {
          const float ha = h0 + (h1 - h0) * k / pieces, hb = h0 + (h1 - h0) * (k + 1) / pieces;
          const float va = ha / tile.y + off.y, vb = hb / tile.y + off.y;
          Quad(side, p(a0, b0, ha), p(a1, b1, ha), p(a1, b1, hb), p(a0, b0, hb), {u0, va}, {u1, va}, {u1, vb},
               {u0, vb}, f.n);
        }
      }
    }
    if (!top.empty()) {
      Quad(top, p(-hw, -hl, h1), p(hw, -hl, h1), p(hw, hl, h1), p(-hw, hl, h1), {0, 0}, {w / 24.0f, 0},
           {w / 24.0f, l / 24.0f}, {0, l / 24.0f}, up);
    }
  }

private:
  ObjData& data_;
};

// Una rejilla de letras: la textura, el orden de las letras en ella y cuantas columnas y filas tiene.
struct GlyphGrid {
  std::string texture;
  std::string glyphs;
  int cols = 8;
  int rows = 5;
};

// Una frase con la rejilla: de izquierda a derecha por 'right', de 'height' de alto, empezando en
// 'origin' (abajo a la izquierda), mirando hacia 'n'. Lo que no esta en la rejilla (un espacio): hueco.
inline void Text(Builder& b, const GlyphGrid& grid, const std::string& text, Vector3 origin, Vector3 right, Vector3 up,
                 float height, Vector3 n) {
  const float w = height * 0.62f;
  for (std::size_t i = 0; i < text.size(); ++i) {
    const std::size_t g = grid.glyphs.find(static_cast<char>(std::toupper(static_cast<unsigned char>(text[i]))));
    if (g == std::string::npos)
      continue;
    const int col = static_cast<int>(g) % grid.cols, row = static_cast<int>(g) / grid.cols;
    const float u0 = static_cast<float>(col) / grid.cols, u1 = static_cast<float>(col + 1) / grid.cols;
    const float vt = 1.0f - static_cast<float>(row) / grid.rows, vb = 1.0f - static_cast<float>(row + 1) / grid.rows;
    const Vector3 o = Vector3Add(origin, Vector3Scale(right, w * 1.08f * static_cast<float>(i)));
    b.Quad(grid.texture, o, Vector3Add(o, Vector3Scale(right, w)),
           Vector3Add(o, Vector3Add(Vector3Scale(right, w), Vector3Scale(up, height))), Vector3Add(o, Vector3Scale(up, height)),
           {u0, vb}, {u1, vb}, {u1, vt}, {u0, vt}, n);
  }
}

inline float TextWidth(const std::string& text, float height) {
  return height * 0.62f * 1.08f * static_cast<float>(text.size());
}

} // namespace retro::gen
