#include "world/CylinderCity.hpp"

#include "assets/AssetManager.hpp"
#include "core/RetroEngine.hpp"
#include "game/Localization.hpp"
#include "world/Entity.hpp"
#include "world/MeshBuilder.hpp"
#include "world/MeshUpload.hpp"
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
using gen::Builder;
using gen::Mix;
using gen::Rng;
using gen::Seed;


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

// La rejilla de letras de la ciudad ('grid': otra rejilla con el mismo orden, para otro fondo).
void Text(Builder& b, const CityConfig& c, const std::string& text, Vector3 origin, Vector3 right, Vector3 up,
          float height, Vector3 n, const std::string& grid = {}) {
  gen::Text(b, {grid.empty() ? c.textures.glyphs : grid, c.glyphs, c.glyphCols, c.glyphRows}, text, origin, right, up,
            height, n);
}

using gen::TextWidth;

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
  bool fronts = false; // el pie en 3D (CityConfig::storefronts)
};

// La puerta del vestibulo de una torre (la deja Storefronts en el local de en medio de 'entranceFace').
struct Entrance {
  bool ok = false;
  Vector3 door;      // donde va el modelo de la puerta (en la ciudad)
  float yaw = 0.0f;  // su giro en los ejes de la torre (el frente mira hacia fuera)
  float across = 0.0f, along = 0.0f, floor = 0.0f; // fuera de la puerta, en los ejes de la torre
  float sign = 1.0f; // hacia que lado de 'across' mira
};

// Las celdas de color liso del atlas de etiquetas (la ultima fila).
namespace lisa {
constexpr int teal = 56, rojo = 57, crema = 58, negro = 59, blanco = 60, acero = 61, madera = 62, gris = 63;
}

// El pie en 3D de una torre de cerca (el zocalo de W x Lz m, en el marco 'f', con la base hundida 2 m),
// igual que su planta baja por dentro: por cada cara, pilastras en las esquinas y entre tramo y tramo, y
// en cada tramo un ventanal (antepecho, canceleria, persianas bajadas hasta donde las dejaron) con el
// vestibulo con luz detras: el piso, el fondo, el plafon con sus lamparas. En la cara 'entranceFace', el
// tramo de en medio es la entrada (hundida, con su vestibulo: elevadores, recepcion, buzones). La tierra
// es curva: cada tramo se apoya en lo mas alto del suyo. Devuelve la altura (en el marco de la torre)
// donde acaba la cornisa: de ahi para arriba, el zocalo de oficinas.
float Storefronts(const CityConfig& c, Builder& solid, Builder& glow, Rng& r, const Frame& f, float W, float Lz,
                  int entranceFace = -1, Entrance* entrance = nullptr) {
  const CityConfig::Storefronts& sf = c.storefronts;
  constexpr float kSink = 2.0f;
  auto groundAt = [&](float across) { return kSink + across * across / (2.0f * c.radius); };
  const float hTop = groundAt(W * 0.5f) + 6.2f;
  const float hO = hTop - 1.6f; // lo alto de los huecos (encima, la caja de luz)

  struct Face {
    Vector3 n;    // hacia fuera
    Vector3 t;    // la derecha de quien la mira desde fuera
    float half;   // del centro de la torre a la cara
    float len;    // lo que mide
    bool across;  // la cara mira a lo ancho (su tierra esta a una sola altura)
  };
  auto face = [&](Vector3 n, float half, float len, bool across) {
    return Face{n, Vector3CrossProduct(Vector3Negate(n), f.up), half, len, across};
  };
  const Face faces[4] = {face(f.across, W * 0.5f, Lz, true), face(Vector3Negate(f.across), W * 0.5f, Lz, true),
                         face(f.along, Lz * 0.5f, W, false), face(Vector3Negate(f.along), Lz * 0.5f, W, false)};

  for (int fi = 0; fi < 4; ++fi) {
    const Face& fc = faces[fi];
    // un punto de la cara: x a la derecha, h hacia arriba, k hacia dentro (negativo: fuera)
    auto P = [&](float x, float h, float k) {
      return Vector3Add(f.base, Vector3Add(Vector3Scale(fc.n, fc.half - k),
                                           Vector3Add(Vector3Scale(fc.t, x), Vector3Scale(f.up, h))));
    };
    auto box = [&](Builder& b, const std::string& side, const std::string& top, float x0, float x1, float h0, float h1,
                   float k0, float k1, Vector2 tile) {
      b.Box(side, top, P((x0 + x1) * 0.5f, 0.0f, (k0 + k1) * 0.5f), f.up, fc.t, fc.n, x1 - x0, k1 - k0, h0, h1, tile,
            {0, 0}, 50.0f);
    };
    // un rectangulo vertical de frente (mira hacia fuera) a la profundidad k
    auto front = [&](Builder& b, const std::string& tex, float x0, float x1, float h0, float h1, float k, Vector2 uv1) {
      b.Quad(tex, P(x0, h0, k), P(x1, h0, k), P(x1, h1, k), P(x0, h1, k), {0, 0}, {uv1.x, 0}, uv1, {0, uv1.y}, fc.n);
    };
    // un lado (de k0 a k1, mirando a 'dir': +1 a la derecha), un piso o un techo, en metros
    auto side = [&](Builder& b, const std::string& tex, float x, float h0, float h1, float k0, float k1, float dir,
                    float tile = 2.0f) {
      const float d = k1 - k0;
      b.Quad(tex, P(x, h0, k0), P(x, h0, k1), P(x, h1, k1), P(x, h1, k0), {0, 0}, {d / tile, 0},
             {d / tile, (h1 - h0) / tile}, {0, (h1 - h0) / tile}, Vector3Scale(fc.t, dir));
    };
    auto flat = [&](Builder& b, const std::string& tex, float x0, float x1, float h, float k0, float k1, bool facingUp,
                    float tile = 2.0f) {
      b.Quad(tex, P(x0, h, k0), P(x1, h, k0), P(x1, h, k1), P(x0, h, k1), {0, 0}, {(x1 - x0) / tile, 0},
             {(x1 - x0) / tile, (k1 - k0) / tile}, {0, (k1 - k0) / tile}, facingUp ? f.up : Vector3Negate(f.up));
    };
    // Una caja con cara de frente y tapa en 'lit' y los lados y el fondo en 'shade' (la luz viene de
    // fuera: los lados, mas oscuros). Sin fondo ni base.
    auto shaded = [&](Builder& b, const std::string& lit, const std::string& shade, float x0, float x1, float h0, float h1,
                      float k0, float k1, float tile = 1.0f) {
      const float w = (x1 - x0) / tile, d = (k1 - k0) / tile, hh = (h1 - h0) / tile;
      b.Quad(lit, P(x0, h0, k0), P(x1, h0, k0), P(x1, h1, k0), P(x0, h1, k0), {0, 0}, {w, 0}, {w, hh}, {0, hh}, fc.n);
      b.Quad(lit, P(x0, h1, k0), P(x1, h1, k0), P(x1, h1, k1), P(x0, h1, k1), {0, 0}, {w, 0}, {w, d}, {0, d}, f.up);
      b.Quad(shade, P(x0, h0, k1), P(x0, h0, k0), P(x0, h1, k0), P(x0, h1, k1), {0, 0}, {d, 0}, {d, hh}, {0, hh},
             Vector3Negate(fc.t));
      b.Quad(shade, P(x1, h0, k0), P(x1, h0, k1), P(x1, h1, k1), P(x1, h1, k0), {0, 0}, {d, 0}, {d, hh}, {0, hh}, fc.t);
    };
    // Un producto (o cualquier cosa con etiqueta): la celda 'cell' del atlas en el frente y, mas oscura,
    // en la tapa y los lados.
    auto goods = [&](Builder& b, int cell, float x0, float x1, float h0, float h1, float k0, float k1) {
      const int col = cell % 8, row = cell / 8;
      const float u0 = (col + 0.04f) / 8.0f, u1 = (col + 0.96f) / 8.0f;
      const float v1 = 1.0f - (row + 0.04f) / 8.0f, v0 = 1.0f - (row + 0.96f) / 8.0f;
      b.Quad(sf.goods, P(x0, h0, k0), P(x1, h0, k0), P(x1, h1, k0), P(x0, h1, k0), {u0, v0}, {u1, v0}, {u1, v1}, {u0, v1}, fc.n);
      b.Quad(sf.goodsShade, P(x0, h1, k0), P(x1, h1, k0), P(x1, h1, k1), P(x0, h1, k1), {u0, v0}, {u1, v0}, {u1, v1},
             {u0, v1}, f.up);
      b.Quad(sf.goodsShade, P(x0, h0, k1), P(x0, h0, k0), P(x0, h1, k0), P(x0, h1, k1), {u0, v0}, {u1, v0}, {u1, v1},
             {u0, v1}, Vector3Negate(fc.t));
      b.Quad(sf.goodsShade, P(x1, h0, k0), P(x1, h0, k1), P(x1, h1, k1), P(x1, h1, k0), {u0, v0}, {u1, v0}, {u1, v1},
             {u0, v1}, fc.t);
    };
    auto ground = [&](float x) { return fc.across ? groundAt(fc.half) : groundAt(x); };

    // las esquinas y la cornisa
    const float L = fc.len;
    box(solid, sf.pillar, sf.pillar, -L * 0.5f, -L * 0.5f + 1.0f, 0.0f, hTop, -0.35f, 0.6f, {2.0f, 4.0f});
    box(solid, sf.pillar, sf.pillar, L * 0.5f - 1.0f, L * 0.5f, 0.0f, hTop, -0.35f, 0.6f, {2.0f, 4.0f});
    box(solid, sf.pillar, sf.pillar, -L * 0.5f - 0.35f, L * 0.5f + 0.35f, hTop, hTop + 0.55f, -0.55f, 0.6f, {2.0f, 4.0f});

    const float inner = L - 2.0f;
    const int bays = std::max(1, static_cast<int>(std::lround(inner / 8.0f)));
    const float bw = (inner - (bays - 1) * 0.7f) / bays;
    for (int b = 0; b < bays; ++b) {
      const float xa = -L * 0.5f + 1.0f + b * (bw + 0.7f), xb = xa + bw;
      if (b > 0)
        box(solid, sf.pillar, sf.pillar, xa - 0.7f, xa, 0.0f, hTop, -0.25f, 0.6f, {2.0f, 4.0f});
      const float fl = std::max(ground(xa), ground(xb)) + 0.12f; // el piso del local
      // el escalon, el muro de encima del hueco y la caja de luz
      box(solid, sf.pillar, sf.pillar, xa, xb, 0.0f, fl, -0.6f, 0.3f, {2.0f, 2.0f});
      front(solid, sf.pillar, xa, xb, hO, hTop, 0.0f, {bw / 2.0f, (hTop - hO) / 2.0f});

      if (fi == entranceFace && b == bays / 2 && entrance) {
        // el vestibulo: 6 m de fondo, con luz; elevadores al fondo, recepcion, buzones, una planta
        const float D = 6.0f, xc = (xa + xb) * 0.5f;
        // (del vidrio hacia dentro: el primer metro, el del marco, va aparte)
        flat(glow, sf.lobbyFloor, xa, xb, fl, 1.0f, D, true, 1.2f);
        flat(glow, sf.ceiling, xa, xb, hO, 1.0f, D, false, 1.2f);
        side(glow, sf.lobbyWall, xa, fl, hO, 1.0f, D, 1.0f, 3.0f);
        side(glow, sf.lobbyWall, xb, fl, hO, 1.0f, D, -1.0f, 3.0f);
        front(glow, sf.lobbyWall, xa, xb, fl, hO, D, {bw / 3.0f, (hO - fl) / 3.0f});
        for (const float k : {2.2f, 4.4f})
          box(glow, sf.light, sf.light, xc - 0.9f, xc + 0.9f, hO - 0.04f, hO - 0.02f, k - 0.3f, k + 0.3f, {1.2f, 1.2f});
        // los dos elevadores: marco, puertas (dos hojas) y el piso donde esta, arriba
        for (const float ex : {xc - 1.3f, xc + 1.3f}) {
          shaded(glow, sf.metal, sf.metalShade, ex - 0.75f, ex + 0.75f, fl, fl + 2.35f, D - 0.06f, D, 0.5f);
          shaded(glow, sf.metal, sf.metalShade, ex - 0.6f, ex - 0.01f, fl, fl + 2.2f, D - 0.08f, D - 0.06f, 1.0f);
          shaded(glow, sf.metal, sf.metalShade, ex + 0.01f, ex + 0.6f, fl, fl + 2.2f, D - 0.08f, D - 0.06f, 1.0f);
          goods(glow, lisa::negro, ex - 0.2f, ex + 0.2f, fl + 2.45f, fl + 2.6f, D - 0.04f, D);
        }
        // recepcion (madera con cubierta), una banca, los buzones contra un lado y una planta
        shaded(glow, sf.wood, sf.woodShade, xa + 0.5f, xa + 2.6f, fl, fl + 1.05f, 3.0f, 3.6f);
        shaded(glow, sf.metal, sf.metalShade, xa + 0.45f, xa + 2.65f, fl + 1.05f, fl + 1.1f, 2.95f, 3.65f);
        shaded(glow, sf.wood, sf.woodShade, xb - 1.9f, xb - 0.3f, fl, fl + 0.45f, 2.0f, 2.4f);
        shaded(glow, sf.mail, sf.metalShade, xb - 0.35f, xb - 0.02f, fl + 0.6f, fl + 2.0f, 3.4f, 5.4f, 1.4f);
        shaded(glow, sf.wood, sf.woodShade, xa + 0.2f, xa + 0.62f, fl, fl + 0.45f, 5.2f, 5.62f);
        shaded(glow, sf.plant, sf.plant, xa + 0.1f, xa + 0.72f, fl + 0.45f, fl + 1.3f, 5.1f, 5.72f, 0.7f);
        shaded(glow, sf.plant, sf.plant, xa + 0.22f, xa + 0.6f, fl + 1.3f, fl + 1.75f, 5.22f, 5.6f, 0.7f);
        // la canceleria (la puerta es un modelo, en medio)
        for (const float x : {xa, xc - 1.3f, xc + 1.3f, xb})
          box(solid, sf.frame, sf.frame, x - 0.06f, x + 0.06f, fl, hO, 0.95f, 1.05f, {1.0f, 1.0f});
        box(solid, sf.frame, sf.frame, xa, xb, fl + 2.75f, fl + 2.85f, 0.95f, 1.05f, {1.0f, 1.0f});
        box(solid, sf.frame, sf.frame, xa, xc - 1.3f, fl, fl + 0.3f, 0.95f, 1.05f, {1.0f, 1.0f});
        box(solid, sf.frame, sf.frame, xc + 1.3f, xb, fl, fl + 0.3f, 0.95f, 1.05f, {1.0f, 1.0f});
        flat(solid, sf.floor, xa, xb, fl, 0.0f, 1.0f, true);
        flat(solid, sf.ceiling, xa, xb, hO, 0.0f, 1.0f, false);
        side(solid, sf.pillar, xa, fl, hO, 0.0f, 1.0f, 1.0f);
        side(solid, sf.pillar, xb, fl, hO, 0.0f, 1.0f, -1.0f);
        entrance->ok = true;
        entrance->door = P(xc, fl, 1.0f);
        entrance->sign = Vector3DotProduct(fc.n, f.across) > 0.0f ? 1.0f : -1.0f;
        entrance->yaw = entrance->sign > 0.0f ? 90.0f : -90.0f;
        entrance->across = entrance->sign * (fc.half + 1.6f);
        entrance->along = Vector3DotProduct(fc.t, f.along) * xc;
        entrance->floor = fl;
        continue;
      }
      // el ventanal: antepecho, el hueco con su derrame, la canceleria, las persianas y el vestibulo detras
      const float sill = fl + 0.9f, top = fl + 2.7f, D = 4.0f;
      front(solid, sf.pillar, xa, xb, fl, sill, 0.0f, {bw / 2.0f, (sill - fl) / 2.0f});
      front(solid, sf.pillar, xa, xb, top, hO, 0.0f, {bw / 2.0f, (hO - top) / 2.0f});
      side(solid, sf.pillar, xa, sill, top, 0.0f, 0.3f, 1.0f);
      side(solid, sf.pillar, xb, sill, top, 0.0f, 0.3f, -1.0f);
      flat(solid, sf.pillar, xa, xb, top, 0.0f, 0.3f, false);
      box(solid, sf.frame, sf.frame, xa - 0.02f, xb + 0.02f, sill - 0.05f, sill, -0.06f, 0.32f, {1.0f, 1.0f}); // el alfeizar
      const int panes = std::max(2, static_cast<int>(std::lround(bw / 1.5f)));
      for (int k = 0; k <= panes; ++k) {
        const float x = xa + bw * k / panes;
        box(solid, sf.frame, sf.frame, x - 0.04f, x + 0.04f, sill, top, 0.26f, 0.32f, {1.0f, 1.0f});
      }
      box(solid, sf.frame, sf.frame, xa, xb, top - 0.06f, top, 0.26f, 0.32f, {1.0f, 1.0f});
      // las persianas: tablillas de lamina, del cabezal hasta donde quedaron (unas hasta abajo)
      const float down = r.Chance(0.2f) ? sill + 0.05f : r.Range(sill + 0.3f, top - 0.2f);
      for (float y = top - 0.08f; y > down; y -= 0.11f)
        box(solid, sf.metal, sf.metal, xa + 0.02f, xb - 0.02f, y - 0.04f, y, 0.36f, 0.4f, {1.0f, 1.0f});
      // el vestibulo, con luz: piso, plafon y sus lamparas, el fondo y los lados (de cada tramo)
      flat(glow, sf.lobbyFloor, xa, xb, fl, 0.3f, D, true, 1.2f);
      flat(glow, sf.ceiling, xa, xb, fl + 3.0f, 0.3f, D, false, 1.2f);
      side(glow, sf.lobbyWall, xa, fl, fl + 3.0f, 0.3f, D, 1.0f, 3.0f);
      side(glow, sf.lobbyWall, xb, fl, fl + 3.0f, 0.3f, D, -1.0f, 3.0f);
      front(glow, sf.lobbyWall, xa, xb, fl, fl + 3.0f, D, {bw / 3.0f, 1.0f});
      const float xm = (xa + xb) * 0.5f;
      for (const float x : {xm - bw * 0.25f, xm + bw * 0.25f})
        box(glow, sf.light, sf.light, x - 0.6f, x + 0.6f, fl + 2.96f, fl + 2.98f, D * 0.5f - 0.3f, D * 0.5f + 0.3f,
            {1.2f, 1.2f});
      if (r.Chance(0.35f)) { // una planta o una banca, de lo que hay dentro
        if (r.Chance(0.5f)) {
          shaded(glow, sf.wood, sf.woodShade, xa + 0.4f, xa + 0.82f, fl, fl + 0.45f, D - 0.6f, D - 0.18f);
          shaded(glow, sf.plant, sf.plant, xa + 0.3f, xa + 0.92f, fl + 0.45f, fl + 1.3f, D - 0.7f, D - 0.08f, 0.7f);
        } else {
          shaded(glow, sf.wood, sf.woodShade, xm - 0.9f, xm + 0.9f, fl, fl + 0.45f, D - 0.6f, D - 0.2f);
        }
      }
    }
  }
  return hTop + 0.55f;
}

// Una torre: un zocalo con tiendas, el fuste, de cero a tres retranqueos y la corona (maquinas,
// a veces un mastil con luz). Con 'detail', ademas: cornisas, costillas, tubos, aparatos, neones y
// su numero. Devuelve hasta donde llega (para los puentes).
// Lo que la ciudad necesita de una torre con locales en 3D para que se entre y se suba: la puerta del
// vestibulo, la caseta de la azotea y sus colisiones (en los ejes de la torre: across = x, arriba = y,
// along = z; la base hundida 2 m, como el zocalo).
struct TowerTop {
  bool ok = false;
  Entrance entrance;
  float roofH = 0.0f;        // la azotea (alto, en el marco de la torre)
  Vector3 hutDoor;           // donde va la puerta de la caseta (en la ciudad)
  float hutX = 0.0f;         // la caseta, en los ejes de la torre
  float w = 0.0f, l = 0.0f;  // lo que mide la azotea
  float crown = 0.0f;
  std::uint64_t seed = 0;
};

float Tower(const CityConfig& c, Builder& solid, Builder& glow, Rng& r, const TowerSpot& s, bool detail, int number,
            const std::string& sector, TowerTop* out = nullptr) {
  const Frame f = FrameAt(c, s.theta, s.z, 2.0f); // la base dos metros hundida (la tierra es curva)
  const float seg = detail ? 30.0f : 400.0f; // de cerca, tramos chicos (el mapeo afin); de lejos, enteras
  const std::string& facade = c.textures.facades[static_cast<std::size_t>(r.Pick(static_cast<int>(c.textures.facades.size())))];
  const Vector2 tile{24.0f, 24.0f};
  const Vector2 off{static_cast<float>(r.Pick(4)) * 0.25f, static_cast<float>(r.Pick(4)) * 0.25f};

  // el zocalo
  const std::size_t podiumIndex = static_cast<std::size_t>(r.Pick(static_cast<int>(c.textures.podiums.size())));
  const std::string& base = c.textures.podiums[podiumIndex];
  const std::string& roof = c.textures.roofs[static_cast<std::size_t>(r.Pick(static_cast<int>(c.textures.roofs.size())))];
  const float podium = r.Range(24.0f, 42.0f);
  const float podiumOff = r.Range(0.0f, 1.0f);
  // el zocalo: una vuelta de textura cada 28 m (cuatro o cinco locales), corrida al azar; de cerca, los
  // locales en 3D y encima sus oficinas
  float frontsTop = 0.0f;
  if (s.fronts) {
    Rng sr(Mix(r.State() ^ 0x10CA1E5ull));
    const int entranceFace = s.off > 0.0f ? 1 : 0; // la cara que da a la avenida (o al centro de su franja)
    frontsTop = Storefronts(c, solid, glow, sr, f, s.w + 6.0f, s.l + 6.0f, out ? entranceFace : -1,
                            out ? &out->entrance : nullptr);
    solid.Box(c.storefronts.tops[podiumIndex], roof, f.base, f.up, f.across, f.along, s.w + 6.0f, s.l + 6.0f, frontsTop,
              podium + 2.0f, {20.0f, 7.5f}, {podiumOff, 0.0f}, seg);
  } else if (c.storefronts.Enabled(c.textures.podiums.size())) {
    // de lejos, el mismo pie sin geometria: la banda de concreto a la altura de la de cerca, su cornisa,
    // los ventanales en una tira con luz (por fuera, apenas) y encima las mismas oficinas
    const float W = s.w + 6.0f, Lz = s.l + 6.0f;
    const float half = W * 0.5f;
    const float hTop = 2.0f + half * half / (2.0f * c.radius) + 6.2f; // como en Storefronts
    const float fl = 2.12f + half * half / (4.0f * c.radius);         // el piso, en promedio
    const CityConfig::Storefronts& sf = c.storefronts;
    solid.Box(sf.pillar, sf.pillar, f.base, f.up, f.across, f.along, W, Lz, 0.0f, hTop, {2.0f, 4.0f}, {0, 0}, seg);
    solid.Box(sf.pillar, sf.pillar, f.base, f.up, f.across, f.along, W + 1.1f, Lz + 1.1f, hTop, hTop + 0.55f, {2.0f, 4.0f},
              {0, 0}, seg);
    glow.Box(sf.farWindows, "", f.base, f.up, f.across, f.along, W + 0.1f, Lz + 0.1f, fl + 0.9f, fl + 2.7f, {34.8f, 1.8f},
             {podiumOff, -(fl + 0.9f) / 1.8f}, seg);
    solid.Box(sf.tops[podiumIndex], roof, f.base, f.up, f.across, f.along, W, Lz, hTop + 0.55f, podium + 2.0f,
              {20.0f, 7.5f}, {podiumOff, 0.0f}, seg);
  } else {
    solid.Box(base, roof, f.base, f.up, f.across, f.along, s.w + 6.0f, s.l + 6.0f, 0.0f, podium + 2.0f,
              {28.0f, podium + 2.0f}, {podiumOff, 0.0f}, seg);
  }
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
  if (out && s.fronts && out->entrance.ok) {
    // la azotea a la que se llega: un pretil alrededor y la caseta de la escalera junto a las maquinas
    for (const float sg : {-1.0f, 1.0f}) {
      solid.Box(c.storefronts.pillar, c.storefronts.pillar, At(f, sg * (w * 0.5f - 0.15f), 0.0f, 0.0f), f.up, f.across,
                f.along, 0.3f, l, h, h + 1.1f, {2.0f, 2.0f}, {0, 0}, 40.0f);
      solid.Box(c.storefronts.pillar, c.storefronts.pillar, At(f, 0.0f, sg * (l * 0.5f - 0.15f), 0.0f), f.up, f.across,
                f.along, w, 0.3f, h, h + 1.1f, {2.0f, 2.0f}, {0, 0}, 40.0f);
    }
    const float hx = w * 0.2f + 1.75f;
    solid.Box(c.storefronts.pillar, roof, At(f, hx, 0.0f, 0.0f), f.up, f.across, f.along, 3.2f, 3.2f, h, h + 3.0f,
              {2.0f, 2.0f}, {0, 0}, 10.0f);
    out->ok = true;
    out->roofH = h;
    out->hutX = hx;
    out->hutDoor = At(f, hx, -1.62f, h);
    out->w = w;
    out->l = l;
    out->crown = crown;
    out->seed = Mix(r.State() ^ 0xED1F1C10ull);
  }
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
    float y = k % 2 == 0 ? r.Range(6.0f, podium - 6.0f) : r.Range(podium + 8.0f, podium + 120.0f);
    if (s.fronts && k % 2 == 0)
      y = std::max(y, frontsTop + 4.0f); // no encima de los locales
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
  const Vector3 o = At(f, toward * ((s.w + 6.0f) * 0.5f + 0.3f), 0.0f, s.fronts ? frontsTop + 0.8f : 9.0f);
  Text(glow, c, plate, Vector3Subtract(o, Vector3Scale(right, TextWidth(plate, ph) * 0.5f)), right, f.up, ph, n);
  return reach;
}


// La tienda de COMIDA: su modelo 3D (una de las variantes) a 'edge' - 15 m del centro de la avenida,
// de un lado o del otro, con el frente hacia la avenida. Aqui va lo que el modelo no trae: las
// colisiones (muros con el hueco de la puerta, gondolas, refrigeradores, mostrador, el piso de
// dentro), las luces, la musica, la campanita, el interior (para saber cuando se esta dentro) y lo que
// se lee (el letrero y dos placas). Las medidas, las del modelo (tools/gen_onus_tienda_modelo.py).
void Store(const CityConfig& c, CylinderCity::ChunkData& out, long long index, float zc, float edge) {
  const CityConfig::Store& st = c.store;
  Rng r(Seed(c.seed, index, 555, static_cast<int>(zc)));
  const float s = r.Chance(0.5f) ? -1.0f : 1.0f; // de que lado de la avenida
  const float W = 12.0f, D = 9.0f, H = 4.2f, door = 1.0f, doorH = 2.6f, t = 0.2f, floorUp = 0.22f;
  const float fx = edge - 15.0f;
  const float xf = s * fx, xb = s * (fx + D), z0 = zc - W * 0.5f, z1 = zc + W * 0.5f;
  const float y = -std::sqrt(c.radius * c.radius - fx * fx);
  const float xfi = xf + s * t, xbi = xb - s * t, z0i = z0 + t, z1i = z1 - t;
  const float yaw = s > 0.0f ? 0.0f : 180.0f; // el modelo mira a -X: del lado +X ya da a la avenida
  // del modelo (x hacia dentro, z a lo largo) al trozo
  auto at = [&](float lx, float ly, float lz) { return Vector3{xf + s * lx, y + ly, zc + s * lz}; };
  auto box = [&](float xa, float xc, float ya, float yc, float za, float zb) {
    out.solids.push_back({{{std::min(xa, xc), ya, std::min(za, zb)}, {std::max(xa, xc), yc, std::max(za, zb)}}, 0.0f, false});
  };

  // el modelo (lo solido, con el letrero para leer) y lo que brilla
  const CityConfig::Store::Variant& v = st.variants[static_cast<std::size_t>(r.Pick(static_cast<int>(st.variants.size())))];
  CylinderCity::ChunkData::Placed shell;
  shell.model = v.model;
  shell.position = at(0.0f, 0.0f, 0.0f);
  shell.yaw = yaw;
  shell.box = {{-0.45f, H - 1.15f, -W * 0.5f + 0.4f}, {0.0f, H - 0.05f, W * 0.5f - 0.4f}, 0.0f};
  shell.prompt = st.signPrompt;
  shell.message = st.signMessage;
  out.placed.push_back(shell);
  out.placed.push_back({v.glow, shell.position, yaw, true, {}, {}, {}});
  // las placas: arriba de los refrigeradores (mira a la puerta) y arriba de la puerta (mira dentro)
  auto plaque = [&](float lx, float ly, float facingYaw) {
    CylinderCity::ChunkData::Placed p;
    p.model = st.plaques[static_cast<std::size_t>(r.Pick(static_cast<int>(st.plaques.size())))];
    p.position = at(lx, ly, 0.0f);
    p.yaw = facingYaw;
    p.box = {{-0.15f, -0.25f, -1.6f}, {0.05f, 0.25f, 1.6f}, 0.0f};
    p.prompt = st.plaquePrompt;
    p.message = st.plaqueMessages[static_cast<std::size_t>(r.Pick(static_cast<int>(st.plaqueMessages.size())))];
    out.placed.push_back(p);
  };
  plaque(D - t - 0.03f, floorUp + 2.97f, yaw);
  plaque(t + 0.03f, 3.47f, yaw + 180.0f);

  // las colisiones: muros (con el hueco de la puerta), el piso de dentro, los muebles
  const float yf = y + floorUp;
  box(xb, xbi, y - 1.0f, y + H, z0, z1);
  box(xf, xb, y - 1.0f, y + H, z0, z0i);
  box(xf, xb, y - 1.0f, y + H, z1i, z1);
  box(xf, xfi, y - 1.0f, y + H, z0, zc - door);
  box(xf, xfi, y - 1.0f, y + H, zc + door, z1);
  box(xf, xfi, y + doorH, y + H, zc - door, zc + door);
  box(xf, xb, yf - 1.0f, yf, z0, z1);
  box(xbi, xbi - s * 0.62f, y - 1.0f, y + H, zc - 3.6f, zc + 3.6f); // los refrigeradores
  for (const float gx : {3.4f, 5.9f}) {                              // las gondolas
    const Vector3 g = at(gx, 0.0f, 0.6f);
    box(g.x - 0.45f, g.x + 0.45f, y - 1.0f, yf + 1.9f, g.z - 3.0f, g.z + 3.0f);
  }
  {
    const Vector3 m = at(1.5f, 0.0f, -3.4f);                         // el mostrador
    box(m.x - 0.45f, m.x + 0.45f, y - 1.0f, yf + 1.05f, m.z - 1.35f, m.z + 1.35f);
    const Vector3 k = at(1.5f, 0.0f, W * 0.5f - t - 0.35f);          // la cafetera
    box(k.x - 0.6f, k.x + 0.6f, y - 1.0f, yf + 1.55f, k.z - 0.35f, k.z + 0.35f);
  }

  // la luz: dos fluorescentes dentro y el letrero fuera
  for (const float dz : {-2.6f, 2.6f}) {
    PointLight l;
    l.position = {xf + s * D * 0.45f, y + H - 0.5f, zc + dz};
    l.color = {1.0f, 1.08f, 1.12f};
    l.radius = 9.0f;
    out.lights.push_back(l);
  }
  {
    PointLight l;
    l.position = {xf - s * 1.6f, y + H - 0.4f, zc};
    l.color = {0.75f, 0.95f, 0.95f};
    l.radius = 11.0f;
    l.flicker = 0.08f;
    out.lights.push_back(l);
  }
  // la musica (dentro y en la puerta), la campanita y el interior
  if (!st.music.empty()) {
    Emitter e;
    e.key = "store:" + std::to_string(index) + ":" + std::to_string(static_cast<int>(zc));
    e.file = st.music;
    e.position = {xf + s * D * 0.5f, y + 1.6f, zc};
    e.volume = st.musicVolume;
    // sin direccion y a todo volumen en todo el local (la esquina mas lejana queda a ~7.5 m del
    // centro); afuera se apaga en unos metros
    e.radius = 14.0f;
    e.minDistance = 7.5f;
    e.directional = false;
    out.emitters.push_back(e);
  }
  out.doors.push_back({{xf + s * t * 0.5f, y + 1.0f, zc}, {0.55f, 1.2f, door}});
  out.interiors.push_back({{std::min(xfi, xbi), y - 0.5f, z0i}, {std::max(xfi, xbi), y + H, z1i}});
}

}  // namespace

// Los metros a lo ancho (por la tierra) de 'from' a 'to' (grados), por el camino corto.
static float ArcBetween(const CityConfig& c, float from, float to) {
  return (std::fmod(to - from + 540.0f, 360.0f) - 180.0f) * kDeg * c.radius;
}

// Todo lo de un trozo: la tierra y las ventanas del anillo y las torres. Con 'detail', ademas, alrededor
// de 'anchor' (grados: por donde anda el jugador) el detalle de las torres y sus colisiones, el suelo
// fino, y si la avenida queda cerca, la calle (farolas, bancas, tiendas, el portico del sector).
static CylinderCity::ChunkData BuildChunk(const CityConfig& c, long long index, bool detail, float anchor) {
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

  // la franja que se camina (abajo, alrededor de la avenida), en grados: un multiplo de las tiras del
  // anillo, para que su borde caiga justo en el de una tira
  const float step = 2.0f;
  // la calle de la avenida (y su pavimento), si quien anda esta cerca de ella
  const bool street = detail && std::fabs(ArcBetween(c, anchor, 0.0f)) < c.detailArc;
  const bool pave = street && !c.textures.pavement.empty();
  // alrededor de quien anda, el anillo en tiras finas: una tira de 2 grados es una cuerda de 52 m que
  // en medio queda 23 cm por encima del circulo (por donde van las colisiones); de 0.25, 4 mm
  const float fineArc = c.walk + c.detailStep;
  const float bandDeg = step * std::ceil((c.walk + 8.0f) / R / kDeg / step);

  // el anillo: tierra (la avenida con su textura) y ventanas, en tiras de 2 grados (cerca de quien
  // anda, de 0.25) y tramos de 60 m
  auto strip = [&](float a, float b) {
    const float mid = (a + b) * 0.5f;
    float center = 0.0f;
    const bool land = onLand(mid, &center);
    if (pave && land && center == 0.0f && std::fabs(mid) < bandDeg)
      return; // lo pone el pavimento de abajo
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
  };
  for (float a = -180.0f; a < 180.0f - 1e-3f; a += step) {
    const bool fine = detail && std::fabs(ArcBetween(c, anchor, a + step * 0.5f)) < fineArc + step * kDeg * R;
    const int parts = fine ? 8 : 1;
    for (int k = 0; k < parts; ++k)
      strip(a + step * k / parts, a + step * (k + 1) / parts);
  }

  // el pavimento de lo que se camina: losas de 'paveTile' m que siguen la curva, cada una con una de
  // las variantes (y volteada o no): de cerca, detalle de verdad; y no se ve el mismo cuadro repetido
  if (pave) {
    int across = static_cast<int>(std::lround(2.0f * bandDeg * kDeg * R / c.paveTile));
    if (across % 2 == 0)
      ++across; // impar: la losa de en medio queda centrada en la avenida (la de la linea de luz)
    const float dTheta = 2.0f * bandDeg / static_cast<float>(across);
    const int along = std::max(1, static_cast<int>(std::lround(L / c.paveTile)));
    const float dz = L / static_cast<float>(along);
    for (int i = 0; i < across; ++i) {
      const float a = -bandDeg + dTheta * i, b = a + dTheta;
      const Frame fa = FrameAt(c, a, 0.0f), fb = FrameAt(c, b, 0.0f);
      const Vector3 n = FrameAt(c, (a + b) * 0.5f, 0.0f).up;
      const bool line = i == across / 2 && !c.textures.pavementLine.empty();
      for (int j = 0; j < along; ++j) {
        Rng r(Seed(c.seed, index, 333, i, j));
        const std::string& tex = line ? c.textures.pavementLine
                                      : c.textures.pavement[static_cast<std::size_t>(r.Pick(static_cast<int>(c.textures.pavement.size())))];
        const float u0 = r.Chance(0.5f) ? 0.0f : 1.0f, v0 = (line || r.Chance(0.5f)) ? 0.0f : 1.0f;
        const float za = dz * j, zb = dz * (j + 1);
        solid.Quad(tex, {fa.base.x, fa.base.y, za}, {fb.base.x, fb.base.y, za}, {fb.base.x, fb.base.y, zb},
                   {fa.base.x, fa.base.y, zb}, {u0, v0}, {1.0f - u0, v0}, {1.0f - u0, 1.0f - v0}, {u0, 1.0f - v0}, n);
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
      if (lc == 0.0f && std::fabs(off) < c.avenue * 0.5f + c.block * 0.3f)
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
        const float arc = std::fabs(ArcBetween(c, anchor, theta));
        s.bottom = arc < c.detailArc;
        const bool near = detail && s.bottom;
        s.fronts = near && arc < c.storefronts.arc && c.storefronts.Enabled(c.textures.podiums.size());
        ++towerNumber;
        TowerTop topInfo;
        const bool enterable = s.fronts && c.interiors.Enabled();
        reach[static_cast<std::size_t>(row)] =
            Tower(c, solid, glow, r, s, near, towerNumber, sector, enterable ? &topInfo : nullptr);
        if (topInfo.ok) {
          // la torre por dentro: tipo y variante con su semilla; un piso cada 3.6 m
          Rng tr(topInfo.seed);
          const std::vector<std::string>* kinds[3] = {&c.interiors.offices, &c.interiors.apartments, &c.interiors.hotel};
          const std::vector<std::string>& list = *kinds[tr.Pick(3)];
          BuildingVisit v;
          v.interior = list[static_cast<std::size_t>(tr.Pick(static_cast<int>(list.size())))];
          v.floors = std::max(12, static_cast<int>(topInfo.roofH / 3.6f));
          v.seed = topInfo.seed;
          v.chunk = index;
          v.theta = theta;
          const float base = -R - 2.0f; // la base de la torre, con la torre abajo
          const Entrance& e = topInfo.entrance;
          v.door = {e.across, base + e.floor + 0.05f, s.z + e.along};
          v.doorYaw = std::atan2(-e.sign, 0.0f) * RAD2DEG; // de espaldas a la puerta
          v.roof = {topInfo.hutX, base + topInfo.roofH + 0.05f, s.z - 3.0f};
          v.roofYaw = 0.0f; // de espaldas a la caseta (mira a -Z)
          const BoxCollider doorBox{{-1.3f, 0.0f, -0.1f}, {1.3f, 2.8f, 0.3f}, 0.0f};
          CylinderCity::ChunkData::Placed lobbyDoor;
          lobbyDoor.model = c.interiors.lobbyDoor;
          lobbyDoor.position = e.door;
          lobbyDoor.yaw = e.yaw;
          lobbyDoor.roll = theta;
          lobbyDoor.box = doorBox;
          lobbyDoor.prompt = c.interiors.enterPrompt;
          lobbyDoor.actions.push_back(action::EnterBuilding{v, false});
          out.placed.push_back(lobbyDoor);
          CylinderCity::ChunkData::Placed hutDoor;
          hutDoor.model = c.interiors.roofDoor;
          hutDoor.position = topInfo.hutDoor;
          hutDoor.yaw = 180.0f;
          hutDoor.roll = theta;
          hutDoor.box = {{-0.6f, 0.0f, -0.1f}, {0.6f, 2.2f, 0.3f}, 0.0f};
          hutDoor.prompt = c.interiors.downPrompt;
          hutDoor.actions.push_back(action::EnterBuilding{v, true});
          out.placed.push_back(hutDoor);
          // la azotea: losa, pretiles, las maquinas y la caseta (derechas en el abajo de la torre)
          const float y0 = base + topInfo.roofH, hw = topInfo.w * 0.5f, hl = topInfo.l * 0.5f;
          auto add = [&](Vector3 a, Vector3 b) { out.solids.push_back({{{a.x, a.y, s.z + a.z}, {b.x, b.y, s.z + b.z}}, theta, false}); };
          add({-hw, y0 - 3.0f, -hl}, {hw, y0, hl});
          add({-hw, y0, -hl}, {-hw + 0.3f, y0 + 1.1f, hl});
          add({hw - 0.3f, y0, -hl}, {hw, y0 + 1.1f, hl});
          add({-hw, y0, -hl}, {hw, y0 + 1.1f, -hl + 0.3f});
          add({-hw, y0, hl - 0.3f}, {hw, y0 + 1.1f, hl});
          add({-topInfo.w * 0.2f, y0, -topInfo.l * 0.2f}, {topInfo.w * 0.2f, y0 + topInfo.crown, topInfo.l * 0.2f});
          add({topInfo.hutX - 1.6f, y0, -1.6f}, {topInfo.hutX + 1.6f, y0 + 3.0f, 1.6f});
        }
        // colisiones de las torres a las que se llega caminando: derechas en su propio abajo
        if (near && arc - s.w * 0.5f - 3.0f < c.walk + c.detailStep) {
          const float grow = s.fronts ? 0.6f : 0.0f; // el escalon de los locales
          const float hw = (s.w + 6.0f) * 0.5f + grow, hl = (s.l + 6.0f) * 0.5f + grow;
          out.solids.push_back({{{-hw, -R - 5.0f, s.z - hl}, {hw, -R + 60.0f, s.z + hl}}, theta, false});
        }
      }
      // puentes entre las dos torres de la columna (cruzan la calle a lo largo)
      if (detail && std::fabs(ArcBetween(c, anchor, theta)) < c.detailArc) {
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

  if (street) {
    // la calle de la avenida: farolas, bancas, quioscos de comida (siempre hay comida)
    auto ground = [&](float x) { return -std::sqrt(R * R - x * x); };
    const Vector3 up{0, 1, 0}, ax{1, 0, 0}, az{0, 0, 1};
    const float edge = c.avenue * 0.5f - 2.0f;
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
    // una tienda cada 'everySectors' sectores (en los que su numero es multiplo), en la primera manzana
    const long long number = c.firstSector + index;
    if (c.store.enabled && ((number % c.store.everySectors) + c.store.everySectors) % c.store.everySectors == 0)
      Store(c, out, index, c.block * 0.5f, edge);
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
  }
  if (detail) {
    // el suelo que se pisa, abajo del mundo (el jugador siempre esta ahi; la ciudad gira debajo):
    // escalones finos que siguen la curva, igual en toda la vuelta
    auto ground = [&](float x) { return -std::sqrt(R * R - x * x); };
    for (float x0 = -48.0f; x0 < 48.0f; x0 += 3.0f) {
      const float x1 = x0 + 3.0f;
      const float top = ground(std::max(std::fabs(x0), std::fabs(x1))); // el borde mas alto: nunca se hunde
      out.solids.push_back({{{x0, top - 6.0f, 0.0f}, {x1, top, L}}, 0.0f, true});
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
  if (cfg_.chunkLength <= 0.0f || cfg_.block <= 0.0f || cfg_.detailStep <= 0.0f || cfg_.radius <= cfg_.maxHeight)
    throw std::runtime_error("city: 'chunkLength', 'block' y 'detailStep' positivos, y 'radius' mayor que 'maxHeight'");
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
  // los modelos de la tienda (varios MB de .obj): que los hilos los vayan leyendo desde ya
  for (const CityConfig::Store::Variant& v : cfg_.store.variants) {
    engine_.Assets().RequestModel(v.model);
    engine_.Assets().RequestModel(v.glow);
  }
  for (const std::string& p : cfg_.store.plaques)
    engine_.Assets().RequestModel(p);
  if (cfg_.interiors.Enabled()) {
    engine_.Assets().RequestModel(cfg_.interiors.lobbyDoor);
    engine_.Assets().RequestModel(cfg_.interiors.roofDoor);
  }
}

CylinderCity::~CylinderCity() = default;

std::shared_ptr<ModelResource> CylinderCity::Upload(const ObjData& data) {
  return UploadMesh(engine_, textures_, data);
}

std::shared_ptr<ModelResource> CylinderCity::Model(const std::string& path) {
  auto& m = models_[path];
  if (!m) {
    m = engine_.Assets().GetModel(path);
    m->SetShader(engine_.Retro().Lit());
    // mipmaps en sus texturas, como las de la ciudad (de lejos, el promedio; de cerca, igual)
    ::Model& raw = m->Get(); // el tipo de raylib (aqui Model es esta funcion)
    for (int i = 0; i < raw.materialCount; ++i) {
      if (raw.materials[i].maps == nullptr)
        continue;
      const Texture2D tx = raw.materials[i].maps[MATERIAL_MAP_DIFFUSE].texture;
      if (tx.id == 0 || tx.width <= 1)
        continue;
      int mips = 1;
      rlGenTextureMipmaps(tx.id, tx.width, tx.height, tx.format, &mips);
      rlTextureParameters(tx.id, RL_TEXTURE_MIN_FILTER, RL_TEXTURE_FILTER_NEAREST_MIP_LINEAR);
    }
  }
  return m;
}

long long CylinderCity::SectorAt(float z) const {
  return cfg_.firstSector + origin_ + static_cast<long long>(std::floor(z / cfg_.chunkLength));
}

void CylinderCity::Drop(Scene& scene, Chunk& chunk) {
  for (const Piece& p : chunk.pieces)
    scene.Remove(p.name);
  for (const auto& [name, door] : chunk.doors)
    std::erase_if(scene.triggers, [&](const Trigger& t) { return t.name == name; });
  chunk.pieces.clear();
  chunk.lights.clear();
  chunk.emitters.clear();
  chunk.solids.clear();
  chunk.doors.clear();
  chunk.interiors.clear();
  chunk.lod = -1;
}

void CylinderCity::Place(Scene& scene, long long index, int lod, long long anchor, ChunkData data) {
  Chunk& chunk = chunks_[index];
  Drop(scene, chunk);
  const float z0 = static_cast<float>(index - origin_) * cfg_.chunkLength;
  const std::string tag = "~city:" + std::to_string(index) + ":";

  // lo solido y lo que brilla (sin luz: emisivo), y las ventanas aparte; las entidades van en el eje
  // (Turn las gira con la ciudad)
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
    e->glowTint = std::string(part.name) == "windows"; // su color lo pone el ciclo (ApplyCycle)
    chunk.pieces.push_back({e->Name(), {0.0f, 0.0f}, 0.0f});
    scene.Add(std::move(e));
  }
  for (std::size_t k = 0; k < data.pods.size(); ++k) {
    const ChunkData::Pod& p = data.pods[k];
    auto e = std::make_unique<Entity>(tag + "pod" + std::to_string(k), podModel_);
    e->transform.position = {p.position.x, p.position.y, z0 + p.position.z};
    e->emissive = true;
    Entity::Drift d;
    d.velocity = {0.0f, 0.0f, p.speed};
    d.length = cfg_.chunkLength;
    d.around = {0.0f, 0.0f, z0 + cfg_.chunkLength * 0.5f};
    d.base = e->transform.position;
    d.ease = 0.0f;
    e->drift = d;
    chunk.pieces.push_back({e->Name(), {p.position.x, p.position.y}, p.roll});
    scene.Add(std::move(e));
  }
  for (PointLight l : data.lights) {
    l.position.z += z0;
    chunk.lights.push_back(l);
  }
  for (Emitter e : data.emitters) {
    e.position.z += z0;
    chunk.emitters.push_back(e);
  }
  for (Solid b : data.solids) {
    b.box.min.z += z0;
    b.box.max.z += z0;
    chunk.solids.push_back(b);
  }
  for (BoundingBox b : data.interiors) {
    b.min.z += z0;
    b.max.z += z0;
    chunk.interiors.push_back(b);
  }
  // los modelos ya hechos (la tienda, sus placas); los que se leen, con E dan su mensaje (traducible)
  for (std::size_t k = 0; k < data.placed.size(); ++k) {
    const ChunkData::Placed& p = data.placed[k];
    auto e = std::make_unique<Entity>(tag + "m" + std::to_string(k), Model(p.model));
    e->transform.position = {p.position.x, p.position.y, z0 + p.position.z};
    e->transform.rotation = {0.0f, p.yaw, 0.0f};
    e->emissive = p.emissive;
    if (!p.message.empty() || !p.actions.empty()) {
      Interaction it;
      it.prompt = p.prompt;
      it.range = p.actions.empty() ? 4.0f : 3.0f;
      it.box = p.box;
      if (p.actions.empty())
        it.actions.push_back(action::Message{p.message, 5.0f});
      else
        it.actions = p.actions;
      e->interact = std::move(it);
    }
    chunk.pieces.push_back({e->Name(), {p.position.x, p.position.y}, p.roll});
    scene.Add(std::move(e));
  }
  // la campanita de cada puerta: un trigger al cruzarla (Turn lo pone en su sitio)
  if (!cfg_.store.chime.empty()) {
    for (std::size_t k = 0; k < data.doors.size(); ++k) {
      ChunkData::Door d = data.doors[k];
      d.center.z += z0;
      Trigger t;
      t.name = tag + "door" + std::to_string(k);
      t.on = TriggerEvent::Enter;
      t.actions.push_back(action::PlaySound{cfg_.store.chime, cfg_.store.chimeVolume});
      scene.triggers.push_back(std::move(t));
      chunk.doors.emplace_back(tag + "door" + std::to_string(k), d);
    }
  }
  chunk.lod = lod;
  chunk.anchor = anchor;
}

void CylinderCity::Turn(Scene& scene) {
  const float phi = static_cast<float>(phi_);
  auto rot = [](float x, float y, float deg) {
    const float a = deg * kDeg, c = std::cos(a), s = std::sin(a);
    return Vector2{x * c - y * s, x * s + y * c};
  };
  auto box = [](Vector3 center, Vector3 half) {
    WorldBox w;
    w.center = center;
    w.halfX = half.x;
    w.halfZ = half.z;
    w.bottom = center.y - half.y;
    w.top = center.y + half.y;
    return w;
  };
  scene.streamedLights.clear();
  scene.streamedEmitters.clear();
  scene.streamedBoxes.clear();
  for (auto& [index, chunk] : chunks_) {
    for (const Piece& p : chunk.pieces) {
      Entity* e = scene.Find(p.name);
      if (!e)
        continue;
      const Vector2 w = rot(p.xy.x, p.xy.y, -phi);
      e->transform.position.x = w.x;
      e->transform.position.y = w.y;
      e->transform.rotation.z = p.roll - phi;
      if (e->drift) {
        e->drift->base.x = w.x;
        e->drift->base.y = w.y;
      }
    }
    for (PointLight l : chunk.lights) {
      const Vector2 w = rot(l.position.x, l.position.y, -phi);
      l.position.x = w.x;
      l.position.y = w.y;
      scene.streamedLights.push_back(l);
    }
    for (Emitter e : chunk.emitters) {
      const Vector2 w = rot(e.position.x, e.position.y, -phi);
      e.position.x = w.x;
      e.position.y = w.y;
      scene.streamedEmitters.push_back(e);
    }
    for (const Solid& s : chunk.solids) {
      Vector3 c = Vector3Scale(Vector3Add(s.box.min, s.box.max), 0.5f);
      const Vector3 half = Vector3Scale(Vector3Subtract(s.box.max, s.box.min), 0.5f);
      if (!s.ground) {
        const Vector2 w = rot(c.x, c.y, s.theta - phi);
        c.x = w.x;
        c.y = w.y;
      }
      scene.streamedBoxes.push_back(box(c, half));
    }
    for (const auto& [name, door] : chunk.doors) {
      for (Trigger& t : scene.triggers) {
        if (t.name != name)
          continue;
        const Vector2 w = rot(door.center.x, door.center.y, -phi);
        t.box = box({w.x, w.y, door.center.z}, door.half);
      }
    }
  }
}

float CylinderCity::AnchorDegrees(long long anchor) const {
  return static_cast<float>(anchor) * cfg_.detailStep / cfg_.radius / kDeg;
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
    for (const Piece& p : chunk.pieces) {
      if (p.name.size() > 8 && p.name.compare(p.name.size() - 8, 8, ":windows") == 0) {
        if (Entity* e = scene.Find(p.name))
          e->tint = win;
      }
    }
  }
}

CylinderCity::Recenter CylinderCity::Update(Scene& scene, Vector3 focus, float dt, bool carried) {
  const float L = cfg_.chunkLength;
  Recenter out;
  bool changed = false;
  // recentrar en Z: si se alejo mas de cuatro trozos del origen, todo se mueve un numero entero de trozos
  const long long away = static_cast<long long>(std::floor(focus.z / L));
  if (std::llabs(away) >= 4) {
    origin_ += away;
    out.dz = static_cast<float>(away) * L;
    scene.ShiftZ(-out.dz);
    for (auto& [index, chunk] : chunks_) {
      for (PointLight& l : chunk.lights)
        l.position.z -= out.dz;
      for (Emitter& e : chunk.emitters)
        e.position.z -= out.dz;
      for (Solid& s : chunk.solids) {
        s.box.min.z -= out.dz;
        s.box.max.z -= out.dz;
      }
      for (auto& [name, door] : chunk.doors)
        door.center.z -= out.dz;
      for (BoundingBox& b : chunk.interiors) {
        b.min.z -= out.dz;
        b.max.z -= out.dz;
      }
    }
    focus.z -= out.dz;
    changed = true;
  }
  // girar: lo que se movio a lo ancho, la ciudad gira para que quien anda vuelva a estar abajo
  if (carried) {
    const double alpha = std::atan2(static_cast<double>(focus.x), -static_cast<double>(focus.y)) / kDeg;
    if (alpha != 0.0) {
      phi_ = std::remainder(phi_ + alpha, 360.0);
      out.turn = static_cast<float>(-alpha);
      focus = {0.0f, -std::hypot(focus.x, focus.y), focus.z};
      changed = true;
    }
  }
  // el paso de detalle: se cambia al pasar tres cuartos de paso (y no se rehace a cada vaiven)
  {
    const double arc = phi_ * kDeg * cfg_.radius, step = cfg_.detailStep;
    const double circle = 2.0 * PI * cfg_.radius;
    const double off = std::remainder(arc - static_cast<double>(anchor_) * step, circle);
    if (std::fabs(off) > step * 0.75)
      anchor_ = std::llround(std::remainder(arc, circle) / step);
  }
  const long long here = origin_ + static_cast<long long>(std::floor(focus.z / L));

  auto wanted = [&](long long i) {
    const long long d = std::llabs(i - here);
    return d <= cfg_.viewChunks ? (d <= cfg_.detailChunks ? 1 : 0) : -1;
  };
  auto key = [&](long long i, int lod) { return std::make_tuple(i, lod, lod == 1 ? anchor_ : 0ll); };

  // al empezar, los de alrededor ya hechos (sin esperar a los hilos)
  if (first_) {
    first_ = false;
    for (long long i = here - 2; i <= here + 2; ++i)
      Place(scene, i, wanted(i), anchor_, BuildChunk(cfg_, i, wanted(i) == 1, AnchorDegrees(anchor_)));
    changed = true;
  }

  // fuera los que ya no hacen falta
  for (auto it = chunks_.begin(); it != chunks_.end();) {
    if (wanted(it->first) < 0) {
      changed = true;
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
      const bool current = found != chunks_.end() && found->second.lod == lod && (lod != 1 || found->second.anchor == anchor_);
      if (current || pending_.contains(key(i, lod)))
        continue;
      const CityConfig cfg = cfg_;
      const float anchor = AnchorDegrees(anchor_);
      pending_.emplace(key(i, lod),
                       engine_.Assets().Loader().Submit([cfg, i, lod, anchor] { return BuildChunk(cfg, i, lod == 1, anchor); }));
      if (d == 0)
        break;
    }
  }
  // subir los que esten listos: pocos por fotograma
  int budget = 2;
  for (auto it = pending_.begin(); it != pending_.end() && budget > 0;) {
    if (it->second.wait_for(std::chrono::seconds(0)) != std::future_status::ready) {
      ++it;
      continue;
    }
    const auto [i, lod, anchor] = it->first;
    ChunkData data = it->second.get();
    it = pending_.erase(it);
    if (wanted(i) != lod || (lod == 1 && anchor != anchor_))
      continue; // ya no hace falta asi
    Place(scene, i, lod, anchor, std::move(data));
    changed = true;
    --budget;
  }
  if (changed)
    Turn(scene);
  // dentro de una tienda? (ahi solo suena su musica); las tiendas estan en los ejes de la ciudad
  {
    const float a = static_cast<float>(phi_) * kDeg;
    const Vector3 f{focus.x * std::cos(a) - focus.y * std::sin(a), focus.x * std::sin(a) + focus.y * std::cos(a), focus.z};
    indoors_ = false;
    for (const auto& [index, chunk] : chunks_) {
      for (const BoundingBox& b : chunk.interiors) {
        if (f.x > b.min.x && f.x < b.max.x && f.z > b.min.z && f.z < b.max.z && f.y > b.min.y && f.y < b.max.y)
          indoors_ = true;
      }
    }
  }
  ApplyCycle(scene, dt);
  return out;
}

} // namespace retro
