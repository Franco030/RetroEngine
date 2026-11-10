#include "world/CylinderCity.hpp"

#include "assets/AssetManager.hpp"
#include "core/RetroEngine.hpp"
#include "game/Localization.hpp"
#include "world/Entity.hpp"
#include "world/Scene.hpp"

#include <raymath.h>
#include <rlgl.h>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <iostream>
#include <string>

namespace retro {

namespace {

constexpr float kDeg = PI / 180.0f;

// --------------------------------------------------------------------------- azar con semilla
// Cada trozo sale de su numero: la misma semilla, la misma ciudad (splitmix64).
std::uint64_t Mix(std::uint64_t x) {
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

private:
  std::uint64_t s_;
};

std::uint64_t Seed(std::uint64_t base, long long chunk, int a, int b = 0, int c = 0) {
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

// --------------------------------------------------------------------------- el trozo
struct Frame {
  Vector3 base;   // en la tierra
  Vector3 up;     // hacia el eje
  Vector3 across; // a lo ancho (hacia donde crece theta)
  Vector3 along;  // a lo largo (Z)
};

Frame FrameAt(const CityConfig& c, float thetaDeg, float z, float sink = 0.0f) {
  const float t = thetaDeg * kDeg;
  const float r = c.radius + sink;
  return {{r * std::sin(t), -r * std::cos(t), z}, {-std::sin(t), std::cos(t), 0.0f}, {std::cos(t), std::sin(t), 0.0f},
          {0.0f, 0.0f, 1.0f}};
}

Vector3 At(const Frame& f, float a, float b, float h) {
  return Vector3Add(f.base, Vector3Add(Vector3Scale(f.across, a), Vector3Add(Vector3Scale(f.along, b), Vector3Scale(f.up, h))));
}

// Una frase con la rejilla de letras ('glyphs'): de izquierda a derecha por 'right', de 'height'
// de alto, empezando en 'origin' (abajo a la izquierda), mirando hacia 'n'.
void Text(Builder& b, const CityConfig& c, const std::string& text, Vector3 origin, Vector3 right, Vector3 up,
          float height, Vector3 n) {
  const float w = height * 0.62f;
  for (std::size_t i = 0; i < text.size(); ++i) {
    const std::size_t g = c.glyphs.find(static_cast<char>(std::toupper(static_cast<unsigned char>(text[i]))));
    if (g == std::string::npos)
      continue; // un espacio o algo que no esta: hueco
    const int col = static_cast<int>(g) % c.glyphCols, row = static_cast<int>(g) / c.glyphCols;
    const float u0 = static_cast<float>(col) / c.glyphCols, u1 = static_cast<float>(col + 1) / c.glyphCols;
    const float vt = 1.0f - static_cast<float>(row) / c.glyphRows, vb = 1.0f - static_cast<float>(row + 1) / c.glyphRows;
    const Vector3 o = Vector3Add(origin, Vector3Scale(right, w * 1.08f * static_cast<float>(i)));
    b.Quad(c.textures.glyphs, o, Vector3Add(o, Vector3Scale(right, w)),
           Vector3Add(o, Vector3Add(Vector3Scale(right, w), Vector3Scale(up, height))), Vector3Add(o, Vector3Scale(up, height)),
           {u0, vb}, {u1, vb}, {u1, vt}, {u0, vt}, n);
  }
}

float TextWidth(const std::string& text, float height) { return height * 0.62f * 1.08f * static_cast<float>(text.size()); }

// Un letrero de neon (una celda de la rejilla 4x4 de 'neon') en la cara de normal 'n'.
void Neon(Builder& b, const CityConfig& c, Rng& r, Vector3 center, Vector3 right, Vector3 up, float w, float h,
          Vector3 n) {
  const int cell = r.Pick(16);
  const float u0 = (cell % 4) / 4.0f, v0 = (cell / 4) / 4.0f;
  const Vector3 hr = Vector3Scale(right, w * 0.5f), hu = Vector3Scale(up, h * 0.5f);
  b.Quad(c.textures.neon, Vector3Subtract(Vector3Subtract(center, hr), hu), Vector3Subtract(Vector3Add(center, hr), hu),
         Vector3Add(Vector3Add(center, hr), hu), Vector3Add(Vector3Subtract(center, hr), hu), {u0, v0}, {u0 + 0.25f, v0},
         {u0 + 0.25f, v0 + 0.25f}, {u0, v0 + 0.25f}, n);
}

std::string Number(long long n) {
  char buf[32];
  std::snprintf(buf, sizeof buf, "%lld", n);
  return buf;
}

struct TowerSpot {
  float theta;
  float off; // metros a lo ancho desde el centro de su franja (para la de abajo: desde la avenida)
  float z;   // en el trozo
  float w, l, height;
  bool bottom;
};

// Una torre: un zocalo con tiendas, el fuste, de cero a tres retranqueos y la corona (maquinas,
// a veces un mastil con luz). Con 'detail', ademas: cornisas, costillas, tubos, aparatos, neones y
// su numero. Devuelve hasta donde llega (para los puentes).
float Tower(const CityConfig& c, Builder& solid, Builder& glow, Rng& r, const TowerSpot& s, bool detail, int number,
            const std::string& sector) {
  const Frame f = FrameAt(c, s.theta, s.z, 2.0f); // la base dos metros hundida (la tierra es curva)
  const float seg = detail ? 30.0f : 400.0f; // de cerca, tramos chicos (el mapeo afin); de lejos, enteras
  const std::string& facade = c.textures.facades[static_cast<std::size_t>(r.Pick(static_cast<int>(c.textures.facades.size())))];
  const Vector2 tile{24.0f, 24.0f};
  const Vector2 off{static_cast<float>(r.Pick(4)) * 0.25f, static_cast<float>(r.Pick(4)) * 0.25f};

  // el zocalo
  const std::string& base = c.textures.podiums[static_cast<std::size_t>(r.Pick(static_cast<int>(c.textures.podiums.size())))];
  const std::string& roof = c.textures.roofs[static_cast<std::size_t>(r.Pick(static_cast<int>(c.textures.roofs.size())))];
  const float podium = r.Range(24.0f, 42.0f);
  // el zocalo: una vuelta de textura cada 28 m (cuatro o cinco locales), corrida al azar
  solid.Box(base, roof, f.base, f.up, f.across, f.along, s.w + 6.0f, s.l + 6.0f, 0.0f, podium + 2.0f,
            {28.0f, podium + 2.0f}, {r.Range(0.0f, 1.0f), 0.0f}, seg);
  float w = s.w, l = s.l, h = podium + 2.0f;
  const int tiers = r.Pick(4);
  for (int t = 0; t <= tiers; ++t) {
    const float top = t == tiers ? s.height : h + (s.height - h) * r.Range(0.45f, 0.72f);
    const std::string& tex = (t > 0 && r.Chance(0.35f)) ? c.textures.facades[static_cast<std::size_t>(r.Pick(static_cast<int>(c.textures.facades.size())))] : facade;
    solid.Box(tex, roof, f.base, f.up, f.across, f.along, w, l, h, top, tile, off, seg);
    if (detail) {
      // cornisas: un anillo de losas cada tantos metros
      const float every = r.Range(30.0f, 56.0f);
      for (float y = h + every; y < top - 6.0f; y += every) {
        solid.Box(c.textures.metal, c.textures.metal, f.base, f.up, f.across, f.along, w + 3.0f, l + 3.0f, y, y + 1.4f,
                  {8.0f, 8.0f}, {0, 0}, seg);
      }
      // costillas verticales en las cuatro caras
      const int ribs = 2 + r.Pick(4);
      for (int k = 0; k < ribs; ++k) {
        const float a = -w * 0.5f + w * (k + 0.5f) / ribs;
        const float b = -l * 0.5f + l * (k + 0.5f) / ribs;
        for (const Vector3 pos : {At(f, a, -l * 0.5f, 0.0f), At(f, a, l * 0.5f, 0.0f), At(f, -w * 0.5f, b, 0.0f),
                                  At(f, w * 0.5f, b, 0.0f)}) {
          solid.Box(c.textures.metal, "", pos, f.up, f.across, f.along, 1.2f, 1.2f, h, top, {4.0f, 16.0f}, {0, 0}, 1e5f);
        }
      }
    }
    h = top;
    if (t < tiers) {
      w *= r.Range(0.62f, 0.84f);
      l *= r.Range(0.62f, 0.84f);
    }
  }
  // la corona: maquinas, y a veces un mastil con su luz roja
  const float crown = r.Range(10.0f, 28.0f);
  solid.Box(c.textures.metal, roof, f.base, f.up, f.across, f.along, w * 0.4f, l * 0.4f, h, h + crown,
            {8.0f, 8.0f}, {0, 0}, seg);
  float reach = h + crown;
  if (r.Chance(0.45f)) {
    const float mast = r.Range(30.0f, 90.0f);
    solid.Box(c.textures.metal, "", f.base, f.up, f.across, f.along, 1.6f, 1.6f, reach, reach + mast, {4.0f, 16.0f}, {0, 0},
              seg);
    glow.Box(c.textures.lamp, c.textures.lamp, f.base, f.up, f.across, f.along, 3.0f, 3.0f, reach + mast, reach + mast + 3.0f,
             {3.0f, 3.0f}, {0, 0}, 10.0f);
    reach += mast;
  }

  if (!detail)
    return reach;

  // tubos que suben por una cara y aparatos colgados de las de abajo
  for (int k = 0, n = 2 + r.Pick(3); k < n; ++k) {
    const float a = r.Range(-s.w * 0.45f, s.w * 0.45f);
    const float side = r.Chance(0.5f) ? -1.0f : 1.0f;
    solid.Box(c.textures.metal, "", At(f, a, side * (s.l * 0.5f + 1.2f), 0.0f), f.up, f.across, f.along, 1.8f, 1.8f,
              podium, r.Range(podium + 60.0f, std::max(podium + 80.0f, s.height * 0.8f)), {4.0f, 16.0f}, {0, 0}, 1e5f);
  }
  for (int k = 0, n = 14 + r.Pick(18); k < n; ++k) {
    const float y = r.Range(podium + 4.0f, podium + 90.0f);
    const float a = r.Range(-s.w * 0.45f, s.w * 0.45f);
    const float side = r.Chance(0.5f) ? -1.0f : 1.0f;
    solid.Box(c.textures.metal, c.textures.metal, At(f, a, side * (s.l * 0.5f + 0.8f), 0.0f), f.up, f.across, f.along, 2.4f,
              1.6f, y, y + 1.4f, {4.0f, 4.0f}, {0, 0}, 10.0f);
  }
  // neones en el zocalo y en lo bajo del fuste, en las caras a lo largo (las que dan a la calle)
  for (int k = 0, n = 6 + r.Pick(8); k < n; ++k) {
    const float side = r.Chance(0.5f) ? -1.0f : 1.0f;
    const float half = (k % 2 == 0 ? s.w + 6.0f : s.w) * 0.5f;
    const float face = (k % 2 == 0 ? s.l + 6.0f : s.l) * 0.5f + 0.4f;
    const float y = k % 2 == 0 ? r.Range(6.0f, podium - 6.0f) : r.Range(podium + 8.0f, podium + 120.0f);
    const bool tall = r.Chance(0.5f);
    const float sw = tall ? r.Range(4.0f, 7.0f) : r.Range(10.0f, 22.0f);
    const float sh = tall ? r.Range(14.0f, 30.0f) : r.Range(4.0f, 8.0f);
    Neon(glow, c, r, At(f, r.Range(-half + sw, half - sw), side * face, y), f.across, f.up, sw, sh,
         Vector3Scale(f.along, side));
  }
  // su numero, en el zocalo, en la cara que da a la avenida
  const float toward = s.off > 0.0f ? -1.0f : 1.0f; // la cara mas cerca del centro de abajo
  const std::string plate = sector + "-" + (number < 10 ? "0" : "") + Number(number);
  const float ph = 2.6f;
  const Vector3 n = Vector3Scale(f.across, toward);
  const Vector3 right = Vector3Scale(f.along, -toward); // de izquierda a derecha, visto desde fuera
  const Vector3 o = At(f, toward * ((s.w + 6.0f) * 0.5f + 0.3f), 0.0f, 9.0f);
  Text(glow, c, plate, Vector3Subtract(o, Vector3Scale(right, TextWidth(plate, ph) * 0.5f)), right, f.up, ph, n);
  return reach;
}

}  // namespace

// Todo lo de un trozo: la tierra y las ventanas del anillo, las torres, y cerca de la avenida el
// detalle, la calle (farolas, quioscos, bancas), los letreros del sector y las colisiones.
static CylinderCity::ChunkData BuildChunk(const CityConfig& c, long long index, bool detail) {
  CylinderCity::ChunkData out;
  Builder solid(out.solid), glow(out.glow);
  ObjData windows;
  Builder win(windows);
  const float L = c.chunkLength, R = c.radius;
  const std::string sector = Number(c.firstSector + index);

  auto onLand = [&](float theta, float* center) {
    for (const float lc : c.lands) {
      const float d = std::fmod(theta - lc + 540.0f, 360.0f) - 180.0f;
      if (std::fabs(d) <= c.landWidth * 0.5f) {
        if (center)
          *center = lc;
        return true;
      }
    }
    return false;
  };

  // el anillo: tierra (la avenida con su textura) y ventanas, en tiras de 2 grados y tramos de 60 m
  const float step = 2.0f;
  for (float a = -180.0f; a < 180.0f - 1e-3f; a += step) {
    const float b = a + step, mid = a + step * 0.5f;
    float center = 0.0f;
    const bool land = onLand(mid, &center);
    const bool avenue = land && center == 0.0f && std::fabs(R * mid * kDeg) < c.avenue * 0.5f + 24.0f;
    const std::string& tex = !land ? c.textures.windows : avenue ? c.textures.avenue : c.textures.ground;
    const float rep = land ? c.block : 60.0f;
    const Frame fa = FrameAt(c, a, 0.0f), fb = FrameAt(c, b, 0.0f);
    const Vector3 n = FrameAt(c, mid, 0.0f).up;
    const float ua = R * a * kDeg / rep, ub = R * b * kDeg / rep;
    const float zoff = static_cast<float>(index % 1000) * L / rep; // que la textura siga de un trozo al otro
    for (int k = 0; k < 4; ++k) {
      const float za = L * k / 4.0f, zb = L * (k + 1) / 4.0f;
      if (land) {
        solid.Quad(tex, {fa.base.x, fa.base.y, za}, {fb.base.x, fb.base.y, za}, {fb.base.x, fb.base.y, zb},
                   {fa.base.x, fa.base.y, zb}, {ua, za / rep + zoff}, {ub, za / rep + zoff}, {ub, zb / rep + zoff},
                   {ua, zb / rep + zoff}, n);
      } else {
        win.Quad(tex, {fa.base.x, fa.base.y, za}, {fb.base.x, fb.base.y, za}, {fb.base.x, fb.base.y, zb},
                {fa.base.x, fa.base.y, zb}, {ua, za / rep}, {ub, za / rep}, {ub, zb / rep}, {ua, zb / rep}, n);
      }
    }
  }

  // las torres
  const int rows = std::max(1, static_cast<int>(std::lround(L / c.block)));
  int towerNumber = 0;
  for (std::size_t li = 0; li < c.lands.size(); ++li) {
    const float lc = c.lands[li];
    const float arc = R * c.landWidth * kDeg;
    const int cols = static_cast<int>(arc / c.block);
    for (int col = 0; col < cols; ++col) {
      const float off = (static_cast<float>(col) - (cols - 1) * 0.5f) * c.block;
      const bool bottomLand = lc == 0.0f;
      if (bottomLand && std::fabs(off) < c.avenue * 0.5f + c.block * 0.3f)
        continue; // la avenida
      const float theta = lc + off / R / kDeg;
      std::vector<float> reach(static_cast<std::size_t>(rows), 0.0f);
      for (int row = 0; row < rows; ++row) {
        Rng r(Seed(c.seed, index, static_cast<int>(li), col, row));
        if (r.Chance(0.05f))
          continue; // una plaza
        TowerSpot s;
        s.theta = theta;
        s.off = off;
        s.z = (row + 0.5f) * c.block;
        s.w = c.block * r.Range(0.5f, 0.78f);
        s.l = c.block * r.Range(0.5f, 0.78f);
        const float u = r.Unit();
        s.height = c.minHeight + (c.maxHeight - c.minHeight) * u * u;
        s.bottom = bottomLand && std::fabs(off) < c.detailArc;
        const bool near = detail && s.bottom;
        ++towerNumber;
        reach[static_cast<std::size_t>(row)] = Tower(c, solid, glow, r, s, near, towerNumber, sector);
        // colisiones de las torres a las que se llega caminando (la inclinacion es poca: se ignora)
        if (near && std::fabs(off) - s.w * 0.5f - 3.0f < c.walk) {
          const Frame f = FrameAt(c, theta, s.z);
          const float hw = (s.w + 6.0f) * 0.5f, hl = (s.l + 6.0f) * 0.5f;
          out.colliders.push_back({{f.base.x - hw, f.base.y - 5.0f, s.z - hl}, {f.base.x + hw, f.base.y + 60.0f, s.z + hl}, 0.0f});
        }
      }
      // puentes entre las dos torres de la columna (cruzan la calle a lo largo)
      if (detail && bottomLand && std::fabs(off) < c.detailArc) {
        for (int row = 0; row + 1 < rows; ++row) {
          const float lo = std::min(reach[static_cast<std::size_t>(row)], reach[static_cast<std::size_t>(row) + 1]);
          Rng r(Seed(c.seed, index, 900 + static_cast<int>(li), col, row));
          for (int k = 0, n = lo > 200.0f ? 1 + r.Pick(3) : 0; k < n; ++k) {
            const float y = r.Range(70.0f, lo * 0.7f);
            const Frame f = FrameAt(c, theta, (row + 1.0f) * c.block, 2.0f);
            solid.Box(c.textures.metal, c.textures.roofs.front(), f.base, f.up, f.across, f.along, r.Range(8.0f, 14.0f),
                      c.block * 0.5f, y, y + r.Range(5.0f, 9.0f), {8.0f, 8.0f}, {0, 0}, 40.0f);
          }
        }
      }
    }
  }

  // las capsulas: por el aire, a lo largo
  {
    Rng r(Seed(c.seed, index, 777));
    for (int k = 0; k < c.pods; ++k) {
      const float lc = c.lands[static_cast<std::size_t>(r.Pick(static_cast<int>(c.lands.size())))];
      const float d = r.Range(-c.landWidth * 0.4f, c.landWidth * 0.4f);
      const float rad = R - r.Range(c.maxHeight * 0.55f, c.maxHeight + 120.0f);
      const float t = (lc + d) * kDeg;
      out.pods.push_back({{rad * std::sin(t), -rad * std::cos(t), r.Range(0.0f, L)}, lc + d,
                          (d < 0.0f ? 1.0f : -1.0f) * c.podSpeed * r.Range(0.8f, 1.2f)});
    }
  }

  if (detail) {
    // la calle de la avenida: farolas, bancas, quioscos de comida (siempre hay comida)
    auto ground = [&](float x) { return -std::sqrt(R * R - x * x); };
    const Vector3 up{0, 1, 0}, ax{1, 0, 0}, az{0, 0, 1};
    const float edge = c.avenue * 0.5f - 2.0f;
    const std::string food = lang::Tr(c.foodLabel);
    for (float z = c.lampEvery * 0.5f; z < L; z += c.lampEvery) {
      for (const float side : {-1.0f, 1.0f}) {
        const float x = side * edge;
        const Vector3 base{x, ground(x), z};
        solid.Box(c.textures.metal, c.textures.metal, base, up, ax, az, 0.6f, 0.6f, 0.0f, 11.0f, {2.0f, 8.0f}, {0, 0}, 11.0f);
        solid.Box(c.textures.metal, c.textures.metal, {x - side * 1.6f, base.y, z}, up, ax, az, 3.6f, 0.4f, 10.6f, 11.0f,
                  {2.0f, 2.0f}, {0, 0}, 2.0f);
        glow.Box(c.textures.lamp, c.textures.lamp, {x - side * 3.0f, base.y, z}, up, ax, az, 1.6f, 0.9f, 10.0f, 10.6f,
                 {2.0f, 2.0f}, {0, 0}, 2.0f);
        PointLight l;
        l.position = {x - side * 3.0f, base.y + 9.6f, z};
        l.color = {1.3f, 1.12f, 0.86f}; // la fuerza va en el color
        l.radius = 26.0f;
        out.lights.push_back(l);
        // dos bancas entre farola y farola
        for (const float dz : {c.lampEvery * 0.25f, c.lampEvery * 0.5f}) {
          const Vector3 bb{x - side * 1.0f, ground(x - side), z + dz};
          solid.Box(c.textures.metal, c.textures.metal, bb, up, ax, az, 0.6f, 2.8f, 0.0f, 0.5f, {2.0f, 2.0f}, {0, 0}, 2.0f);
          solid.Box(c.textures.metal, c.textures.metal, {bb.x + side * 0.35f, bb.y, bb.z}, up, ax, az, 0.12f, 2.8f, 0.5f,
                    1.1f, {2.0f, 2.0f}, {0, 0}, 2.0f);
        }
      }
    }
    for (float z = c.block * 0.5f; z < L; z += c.block) {
      Rng r(Seed(c.seed, index, 555, static_cast<int>(z)));
      const float side = r.Chance(0.5f) ? -1.0f : 1.0f;
      const float x = side * (edge - 10.0f);
      const Vector3 base{x, ground(x), z};
      solid.Box(c.textures.podiums[static_cast<std::size_t>(r.Pick(static_cast<int>(c.textures.podiums.size())))],
                c.textures.metal, base, up, ax, az, 5.0f, 4.0f, 0.0f, 3.4f, {8.0f, 8.0f}, {0, 0}, 4.0f);
      solid.Box(c.textures.metal, c.textures.metal, base, up, ax, az, 6.4f, 5.4f, 3.4f, 3.7f, {4.0f, 4.0f}, {0, 0}, 4.0f);
      const Vector3 n{-side, 0, 0};
      const Vector3 right{0, 0, side}; // visto desde la avenida
      const float th = 0.8f;
      Text(glow, c, food, {x - side * 3.3f, base.y + 3.9f, z - side * TextWidth(food, th) * 0.5f}, right, up, th, n);
      out.colliders.push_back({{x - 2.6f, base.y - 1.0f, z - 2.1f}, {x + 2.6f, base.y + 3.7f, z + 2.1f}, 0.0f});
      PointLight l;
      l.position = {x - side * 4.0f, base.y + 3.0f, z};
      l.color = {0.68f, 0.81f, 0.9f};
      l.radius = 12.0f;
      l.flicker = 0.15f;
      out.lights.push_back(l);
    }
    // el portico del sector: dos pilares y una viga que cruza la avenida, con "SECTOR N" por las
    // dos caras (se lee al llegar de cualquier lado)
    {
      const std::string label = lang::Tr(c.sectorLabel) + " " + sector;
      const float z = L * 0.25f, span = edge + 3.0f, th = 4.0f, y0 = 20.0f;
      for (const float side : {-1.0f, 1.0f})
        solid.Box(c.textures.metal, c.textures.metal, {side * span, ground(span), z}, up, ax, az, 1.6f, 1.6f, -2.0f,
                  y0 + 7.0f, {4.0f, 8.0f}, {0, 0}, 40.0f);
      solid.Box(c.textures.metal, c.textures.metal, {0.0f, ground(0.0f), z}, up, ax, az, span * 2.0f + 1.6f, 1.2f, y0,
                y0 + 6.0f, {8.0f, 6.0f}, {0, 0}, 40.0f);
      const float tw = TextWidth(label, th);
      // de frente a quien va hacia +Z (la cara de -Z): su derecha es -X
      Text(glow, c, label, {tw * 0.5f, ground(0.0f) + y0 + 1.0f, z - 0.65f}, {-1, 0, 0}, up, th, {0, 0, -1});
      Text(glow, c, label, {-tw * 0.5f, ground(0.0f) + y0 + 1.0f, z + 0.65f}, {1, 0, 0}, up, th, {0, 0, 1});
    }
    // el suelo que se pisa: escalones finos que siguen la curva (los sube el jugador) y paredes a
    // los lados de lo que se camina
    for (float x0 = -c.walk; x0 < c.walk; x0 += 3.0f) {
      const float x1 = x0 + 3.0f;
      const float top = ground(std::max(std::fabs(x0), std::fabs(x1))); // el borde mas alto: nunca se hunde
      out.colliders.push_back({{x0, top - 6.0f, 0.0f}, {x1, top, L}, 0.0f});
    }
    for (const float side : {-1.0f, 1.0f}) {
      const float x = side * c.walk;
      out.colliders.push_back({{std::min(x, x + side * 2.0f), ground(x) - 10.0f, 0.0f},
                               {std::max(x, x + side * 2.0f), ground(x) + 200.0f, L}, 0.0f});
    }
  }

  // las ventanas van aparte: su color sigue al ciclo
  for (ObjData::Part& p : windows.parts)
    out.glow.parts.push_back(std::move(p));
  return out;
}

// ---------------------------------------------------------------------------
CylinderCity::CylinderCity(CityConfig config, RetroEngine& engine) : cfg_(std::move(config)), engine_(engine) {
  if (cfg_.textures.facades.empty())
    throw std::runtime_error("city: 'facades' necesita al menos una textura");
  if (cfg_.chunkLength <= 0.0f || cfg_.block <= 0.0f || cfg_.radius <= cfg_.maxHeight)
    throw std::runtime_error("city: 'chunkLength' y 'block' positivos, y 'radius' mayor que 'maxHeight'");
  // la capsula, una sola vez
  ObjData pod;
  Builder b(pod);
  b.Box(cfg_.textures.pod, cfg_.textures.pod, {0, -3.0f, -20.0f}, {0, 1, 0}, {1, 0, 0}, {0, 0, 1}, 7.0f, 40.0f, 0.0f, 6.0f,
        {40.0f, 6.0f}, {0, 0}, 6.0f);
  // la caja va de z -20 a 20 con el centro en z 0: se corre para que la base quede en el centro
  for (ObjData::Part& p : pod.parts) {
    for (std::size_t i = 2; i < p.positions.size(); i += 3)
      p.positions[i] += 20.0f;
  }
  podModel_ = Upload(pod);
}

CylinderCity::~CylinderCity() = default;

std::shared_ptr<ModelResource> CylinderCity::Upload(const ObjData& data) {
  std::vector<std::shared_ptr<TextureResource>> textures;
  for (const ObjData::Part& p : data.parts) {
    auto& t = textures_[p.texture];
    if (!t) {
      t = engine_.Assets().GetTexture(p.texture);
      SetTextureWrap(t->Get(), TEXTURE_WRAP_REPEAT);
      // Con mipmaps: de cerca, pixeles (lo de siempre); de lejos, el promedio. Sin ellos, a cientos
      // de metros cada ventana es menor que un pixel y la fachada hierve como granito.
      const Texture2D& tx = t->Get();
      int mips = 1;
      rlGenTextureMipmaps(tx.id, tx.width, tx.height, tx.format, &mips);
      rlTextureParameters(tx.id, RL_TEXTURE_MIN_FILTER, RL_TEXTURE_FILTER_NEAREST_MIP_LINEAR);
    }
    textures.push_back(t);
  }
  auto model = std::make_shared<ModelResource>(data, std::move(textures));
  model->SetShader(engine_.Retro().Lit());
  return model;
}

long long CylinderCity::SectorAt(float z) const {
  return cfg_.firstSector + origin_ + static_cast<long long>(std::floor(z / cfg_.chunkLength));
}

void CylinderCity::Drop(Scene& scene, Chunk& chunk) {
  for (const std::string& name : chunk.entities)
    scene.Remove(name);
  chunk.entities.clear();
  chunk.lights.clear();
  chunk.lod = -1;
}

void CylinderCity::Place(Scene& scene, long long index, int lod, ChunkData data) {
  Chunk& chunk = chunks_[index];
  Drop(scene, chunk);
  const float z0 = static_cast<float>(index - origin_) * cfg_.chunkLength;
  const std::string tag = "~city:" + std::to_string(index) + ":";

  // lo solido (con sus colisiones) y lo que brilla (sin luz: emisivo), y las ventanas aparte
  ObjData windows;
  ObjData glow;
  for (ObjData::Part& p : data.glow.parts)
    (p.texture == cfg_.textures.windows ? windows : glow).parts.push_back(std::move(p));
  const struct {
    const char* name;
    ObjData* obj;
    bool emissive;
  } parts[3] = {{"solid", &data.solid, false}, {"glow", &glow, true}, {"windows", &windows, true}};
  for (const auto& part : parts) {
    if (part.obj->parts.empty())
      continue;
    auto e = std::make_unique<Entity>(tag + part.name, Upload(*part.obj));
    e->transform.position = {0.0f, 0.0f, z0};
    e->emissive = part.emissive;
    if (std::string(part.name) == "solid")
      e->colliders = std::move(data.colliders);
    chunk.entities.push_back(e->Name());
    scene.Add(std::move(e));
  }
  for (std::size_t k = 0; k < data.pods.size(); ++k) {
    const ChunkData::Pod& p = data.pods[k];
    auto e = std::make_unique<Entity>(tag + "pod" + std::to_string(k), podModel_);
    e->transform.position = {p.position.x, p.position.y, z0 + p.position.z};
    e->transform.rotation = {0.0f, 0.0f, p.roll};
    e->emissive = true;
    Entity::Drift d;
    d.velocity = {0.0f, 0.0f, p.speed};
    d.length = cfg_.chunkLength;
    d.around = {0.0f, 0.0f, z0 + cfg_.chunkLength * 0.5f};
    d.base = e->transform.position;
    d.ease = 0.0f;
    e->drift = d;
    chunk.entities.push_back(e->Name());
    scene.Add(std::move(e));
  }
  for (PointLight l : data.lights) {
    l.position.z += z0;
    chunk.lights.push_back(l);
  }
  chunk.lod = lod;
}

void CylinderCity::ApplyCycle(Scene& scene, float dt) {
  time_ += dt;
  const CityConfig::Cycle& cy = cfg_.cycle;
  float day = 1.0f;
  if (cy.seconds > 0.0f) {
    const float phase = cy.start + time_ / cy.seconds;
    // un dia largo y una noche larga, con amaneceres cortos
    const float s = 0.5f + 0.5f * std::cos(2.0f * PI * phase);
    day = std::clamp((s - 0.3f) / 0.4f, 0.0f, 1.0f);
    day = day * day * (3.0f - 2.0f * day);
  }
  auto mix = [&](Color a, Color b) {
    return Color{static_cast<unsigned char>(a.r + (b.r - a.r) * day), static_cast<unsigned char>(a.g + (b.g - a.g) * day),
                 static_cast<unsigned char>(a.b + (b.b - a.b) * day), 255};
  };
  engine_.SetClearColor(mix(cy.nightFog, cy.dayFog));
  RetroShaderParams p = engine_.Retro().Params();
  p.ambient = cy.nightAmbient + (cy.dayAmbient - cy.nightAmbient) * day;
  p.sun = cy.nightSun + (cy.daySun - cy.nightSun) * day;
  engine_.Retro().SetParams(p);
  const Color win = mix(cy.nightWindows, cy.dayWindows);
  for (auto& [index, chunk] : chunks_) {
    for (const std::string& name : chunk.entities) {
      if (name.size() > 8 && name.compare(name.size() - 8, 8, ":windows") == 0) {
        if (Entity* e = scene.Find(name))
          e->tint = win;
      }
    }
  }
}

float CylinderCity::Update(Scene& scene, Vector3 focus, float dt) {
  const float L = cfg_.chunkLength;
  // recentrar: si se alejo mas de cuatro trozos del origen, todo se mueve un numero entero de trozos
  float shift = 0.0f;
  const long long away = static_cast<long long>(std::floor(focus.z / L));
  if (std::llabs(away) >= 4) {
    origin_ += away;
    shift = static_cast<float>(away) * L;
    scene.ShiftZ(-shift);
    for (auto& [index, chunk] : chunks_) {
      for (PointLight& l : chunk.lights)
        l.position.z -= shift;
    }
    focus.z -= shift;
  }
  const long long here = origin_ + static_cast<long long>(std::floor(focus.z / L));

  auto wanted = [&](long long i) {
    const long long d = std::llabs(i - here);
    return d <= cfg_.viewChunks ? (d <= cfg_.detailChunks ? 1 : 0) : -1;
  };

  // al empezar, los de alrededor ya hechos (sin esperar a los hilos)
  if (first_) {
    first_ = false;
    for (long long i = here - 2; i <= here + 2; ++i)
      Place(scene, i, wanted(i), BuildChunk(cfg_, i, wanted(i) == 1));
  }

  // fuera los que ya no hacen falta
  for (auto it = chunks_.begin(); it != chunks_.end();) {
    if (wanted(it->first) < 0) {
      Drop(scene, it->second);
      it = chunks_.erase(it);
    } else {
      ++it;
    }
  }
  // pedir a los hilos los que faltan (o los que cambian de detalle), del centro hacia fuera
  for (long long d = 0; d <= cfg_.viewChunks; ++d) {
    for (const long long i : {here - d, here + d}) {
      const int lod = wanted(i);
      const auto found = chunks_.find(i);
      if ((found != chunks_.end() && found->second.lod == lod) || pending_.contains({i, lod}))
        continue;
      const CityConfig cfg = cfg_;
      pending_.emplace(std::make_pair(i, lod),
                       engine_.Assets().Loader().Submit([cfg, i, lod] { return BuildChunk(cfg, i, lod == 1); }));
      if (d == 0)
        break;
    }
  }
  // subir los que esten listos: pocos por fotograma
  int budget = 2;
  bool lightsChanged = shift != 0.0f;
  for (auto it = pending_.begin(); it != pending_.end() && budget > 0;) {
    if (it->second.wait_for(std::chrono::seconds(0)) != std::future_status::ready) {
      ++it;
      continue;
    }
    const auto [i, lod] = it->first;
    ChunkData data = it->second.get();
    it = pending_.erase(it);
    if (wanted(i) != lod)
      continue; // ya no hace falta asi
    Place(scene, i, lod, std::move(data));
    lightsChanged = true;
    --budget;
  }
  if (lightsChanged) {
    scene.streamedLights.clear();
    for (const auto& [index, chunk] : chunks_)
      scene.streamedLights.insert(scene.streamedLights.end(), chunk.lights.begin(), chunk.lights.end());
  }
  ApplyCycle(scene, dt);
  return shift;
}

} // namespace retro
