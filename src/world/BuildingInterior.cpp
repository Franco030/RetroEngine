#include "world/BuildingInterior.hpp"

#include "assets/AssetManager.hpp"
#include "core/RetroEngine.hpp"
#include "world/Entity.hpp"
#include "world/Logic.hpp"
#include "world/Scene.hpp"

#include <raymath.h>

#include <algorithm>
#include <cmath>
#include <string>
#include <utility>

namespace retro {

namespace {

using gen::Builder;
using gen::Rng;

// --------------------------------------------------------------------------- medidas de la planta
constexpr float kHX = 18.0f, kHZ = 12.0f; // media planta (36 x 24 m)
constexpr float kExt = 0.3f;              // grueso del muro de fuera
constexpr float kCeil = 3.0f;             // el plafon
constexpr float kPart = 0.12f;            // grueso de los muros de dentro (cada cuarto el suyo)
constexpr float kDoorW = 1.0f, kDoorH = 2.15f;
// el cubo de la escalera (por dentro) y el bloque de los elevadores
constexpr float kSX0 = -6.0f, kSX1 = -2.0f, kSZ0 = -3.0f, kSZ1 = 3.0f;
constexpr float kIX0 = -5.85f, kIX1 = -2.15f, kIZ0 = -2.85f, kIZ1 = 2.85f;
constexpr float kLand = 1.0f;             // lo que miden los descansos
constexpr float kMid = -4.0f;             // el muro de en medio de la escalera
constexpr float kStairDoor = -4.0f;       // donde esta la puerta de la escalera (x)
constexpr float kEX0 = -2.0f, kEX1 = 4.0f;
constexpr int kRisers = 11;               // escalones por tramo

struct Rect {
  float x0, z0, x1, z1;
  float W() const { return x1 - x0; }
  float D() const { return z1 - z0; }
};

enum class Mode { Open, Ring, Straight, Lobby };

// Un cuarto: su rectangulo y el lado que da al pasillo (0 -X, 1 +X, 2 -Z, 3 +Z).
struct Room {
  Rect r;
  int side = 2;
  bool open = false;    // con la puerta abierta y amueblado (o, en oficinas, frente de vidrio)
  bool door = true;     // sin puerta: un cuarto de servicio cerrado
  int number = 0;
};

// Arma la malla de la planta: lo solido (con colisiones) y lo que brilla.
class Mesher {
public:
  Mesher(ObjData& solid, ObjData& glow, std::vector<BoxCollider>& colliders)
      : solid_(solid), glow_(glow), colliders_(colliders) {}

  enum Face { XN = 1, XP = 2, YN = 4, YP = 8, ZN = 16, ZP = 32, ALL = 63, SIDES = XN | XP | ZN | ZP, NOBOTTOM = ALL & ~YN };

  // Una caja derecha, con las uv en metros ('tile' m por vuelta): las caras vecinas siguen la textura.
  void Cube(const std::string& tex, Vector3 a, Vector3 b, float tile = 1.0f, int faces = NOBOTTOM, bool glow = false,
            bool collide = false) {
    Builder& B = glow ? glow_ : solid_;
    const float u = 1.0f / tile;
    if (faces & XN)
      B.Quad(tex, {a.x, a.y, b.z}, {a.x, a.y, a.z}, {a.x, b.y, a.z}, {a.x, b.y, b.z}, {b.z * u, a.y * u}, {a.z * u, a.y * u},
             {a.z * u, b.y * u}, {b.z * u, b.y * u}, {-1, 0, 0});
    if (faces & XP)
      B.Quad(tex, {b.x, a.y, a.z}, {b.x, a.y, b.z}, {b.x, b.y, b.z}, {b.x, b.y, a.z}, {a.z * u, a.y * u}, {b.z * u, a.y * u},
             {b.z * u, b.y * u}, {a.z * u, b.y * u}, {1, 0, 0});
    if (faces & ZN)
      B.Quad(tex, {a.x, a.y, a.z}, {b.x, a.y, a.z}, {b.x, b.y, a.z}, {a.x, b.y, a.z}, {a.x * u, a.y * u}, {b.x * u, a.y * u},
             {b.x * u, b.y * u}, {a.x * u, b.y * u}, {0, 0, -1});
    if (faces & ZP)
      B.Quad(tex, {b.x, a.y, b.z}, {a.x, a.y, b.z}, {a.x, b.y, b.z}, {b.x, b.y, b.z}, {b.x * u, a.y * u}, {a.x * u, a.y * u},
             {a.x * u, b.y * u}, {b.x * u, b.y * u}, {0, 0, 1});
    if (faces & YP)
      B.Quad(tex, {a.x, b.y, a.z}, {b.x, b.y, a.z}, {b.x, b.y, b.z}, {a.x, b.y, b.z}, {a.x * u, a.z * u}, {b.x * u, a.z * u},
             {b.x * u, b.z * u}, {a.x * u, b.z * u}, {0, 1, 0});
    if (faces & YN)
      B.Quad(tex, {a.x, a.y, b.z}, {b.x, a.y, b.z}, {b.x, a.y, a.z}, {a.x, a.y, a.z}, {a.x * u, b.z * u}, {b.x * u, b.z * u},
             {b.x * u, a.z * u}, {a.x * u, a.z * u}, {0, -1, 0});
    if (collide)
      Solid(a, b);
  }
  // Una caja que estorba (sin dibujo).
  void Solid(Vector3 a, Vector3 b) { colliders_.push_back({a, b, 0.0f}); }

  // Un piso o un techo (rectangulo horizontal a la altura y).
  void Flat(const std::string& tex, const Rect& r, float y, bool up, float tile, bool glow = false) {
    Cube(tex, {r.x0, y, r.z0}, {r.x1, y, r.z1}, tile, up ? YP : YN, glow);
  }
  // Un rectangulo vertical en el plano x = cte (mirando a +X o -X) o z = cte.
  void WallX(const std::string& tex, float x, float z0, float z1, float y0, float y1, bool facingPlus, float tile,
             bool glow = false) {
    Cube(tex, {x, y0, z0}, {x, y1, z1}, tile, facingPlus ? XP : XN, glow);
  }
  void WallZ(const std::string& tex, float z, float x0, float x1, float y0, float y1, bool facingPlus, float tile,
             bool glow = false) {
    Cube(tex, {x0, y0, z}, {x1, y1, z}, tile, facingPlus ? ZP : ZN, glow);
  }
  // Una imagen entera (uv 0..1) en un rectangulo vertical que mira a 'n' (+-X o +-Z).
  void Picture(const std::string& tex, Vector3 center, Vector3 n, float w, float h, bool glow) {
    Builder& B = glow ? glow_ : solid_;
    const Vector3 right = Vector3CrossProduct(Vector3Negate(n), {0, 1, 0});
    const Vector3 hr = Vector3Scale(right, w * 0.5f), hu{0, h * 0.5f, 0};
    B.Quad(tex, Vector3Subtract(Vector3Subtract(center, hr), hu), Vector3Subtract(Vector3Add(center, hr), hu),
           Vector3Add(Vector3Add(center, hr), hu), Vector3Add(Vector3Subtract(center, hr), hu), {0, 0}, {1, 0}, {1, 1},
           {0, 1}, n);
  }
  Builder& Glow() { return glow_; }

private:
  Builder solid_, glow_;
  std::vector<BoxCollider>& colliders_;
};

// La caja de 'a' a 'b' en orden (para no pensar en cual es menor).
std::pair<Vector3, Vector3> Sorted(Vector3 a, Vector3 b) {
  return {{std::min(a.x, b.x), std::min(a.y, b.y), std::min(a.z, b.z)}, {std::max(a.x, b.x), std::max(a.y, b.y), std::max(a.z, b.z)}};
}

// Hacia donde mira 'yaw' (la convencion del jugador: 0 mira a -Z).
float YawToward(float dx, float dz) { return std::atan2(-dx, -dz) * RAD2DEG; }

} // namespace

BuildingInterior::BuildingInterior(BuildingConfig config, BuildingVisit visit, RetroEngine& engine, Scene& scene)
    : cfg_(std::move(config)), visit_(std::move(visit)), engine_(engine) {
  visit_.floors = std::max(2, visit_.floors);
  visit_.floor = std::clamp(visit_.floor, 0, Top() + 1);
  const float H = cfg_.floorHeight;
  if (visit_.floor > Top()) {
    // se baja de la azotea: al descanso de arriba del ultimo piso, del lado del tramo que baja
    visit_.floor = Top();
    scene.spawn.position = {kStairDoor + 1.0f, H, kIZ1 - 0.5f};
    scene.spawn.yaw = YawToward(0.0f, -1.0f);
  } else {
    scene.spawn.position = {0.0f, 0.0f, -kHZ + 1.5f};
    scene.spawn.yaw = YawToward(0.0f, 1.0f);
  }
  Build(scene);
}

BuildingInterior::~BuildingInterior() = default;

float BuildingInterior::Update(Scene& scene, Vector3 feet) {
  const float H = cfg_.floorHeight;
  const bool inShaft = feet.x > kSX0 && feet.x < kSX1 && feet.z > kSZ0 && feet.z < kSZ1;
  if (!inShaft)
    return 0.0f;
  if (feet.y >= H - 0.05f && visit_.floor < Top()) {
    ++visit_.floor;
    Build(scene);
    return -H;
  }
  if (feet.y < -0.05f && visit_.floor > 0) {
    --visit_.floor;
    Build(scene);
    return H;
  }
  return 0.0f;
}

void BuildingInterior::Build(Scene& scene) {
  for (const std::string& n : entities_)
    scene.Remove(n);
  entities_.clear();
  scene.streamedLights.clear();

  const BuildingConfig::Textures& T = cfg_.textures;
  const float H = cfg_.floorHeight;
  const int floor = visit_.floor, top = Top();
  const bool lobby = floor == 0;
  Rng r(gen::Seed(visit_.seed, floor, 31));

  ObjData solidData, glowData;
  std::vector<BoxCollider> colliders;
  Mesher m(solidData, glowData, colliders);
  std::vector<PointLight> lights;
  auto light = [&](float x, float z) {
    PointLight l;
    l.position = {x, kCeil - 0.3f, z};
    l.color = {0.95f, 0.97f, 1.0f};
    l.radius = 7.5f;
    lights.push_back(l);
  };
  auto panel = [&](float x, float z) { // una lampara del plafon
    m.Cube(T.light, {x - 0.6f, kCeil - 0.02f, z - 0.3f}, {x + 0.6f, kCeil - 0.01f, z + 0.3f}, 1.2f, Mesher::YN, true);
  };
  auto plate = [&](const std::string& text, Vector3 center, Vector3 n, float h) {
    // el numero en su placa oscura
    const float w = gen::TextWidth(text, h) + 0.16f;
    const Vector3 right = Vector3CrossProduct(Vector3Negate(n), {0, 1, 0});
    const Vector3 back = Vector3Subtract(center, Vector3Scale(n, 0.02f));
    const auto [a, b] = Sorted(Vector3Add(Vector3Subtract(back, Vector3Scale(right, w * 0.5f)), {0, -h * 0.7f, 0}),
                               Vector3Add(Vector3Add(center, Vector3Scale(right, w * 0.5f)), {0, h * 0.7f, 0}));
    m.Cube(T.metal, a, b, 0.5f);
    const Vector3 origin = Vector3Add(Vector3Add(center, Vector3Scale(n, 0.005f)),
                                      Vector3Add(Vector3Scale(right, -(w - 0.16f) * 0.5f), {0, -h * 0.5f, 0}));
    gen::Text(m.Glow(), cfg_.glyphs, text, origin, right, {0, 1, 0}, h, n);
  };

  // --------------------------------------------------------------------- la planta: el modo y los cuartos
  Mode mode = Mode::Open;
  float uw = 5.0f;
  switch (cfg_.kind) {
  case BuildingConfig::Kind::Offices:
    mode = cfg_.layout == 0 ? Mode::Open : cfg_.layout == 1 ? Mode::Ring : Mode::Straight;
    uw = cfg_.layout == 1 ? 4.5f : 5.0f;
    break;
  case BuildingConfig::Kind::Apartments:
    mode = cfg_.layout == 1 ? Mode::Straight : Mode::Ring;
    uw = cfg_.layout == 2 ? 9.0f : 6.0f;
    break;
  case BuildingConfig::Kind::Hotel:
    mode = cfg_.layout == 0 ? Mode::Straight : Mode::Ring;
    uw = cfg_.layout == 2 ? 5.5f : 4.0f;
    break;
  }
  if (lobby)
    mode = Mode::Lobby;

  std::vector<Rect> corridors, open; // lo que se pisa: pasillos y espacio abierto
  std::vector<Room> rooms;
  auto band = [&](Rect b, int side, bool alongX, float width, bool withDoor) {
    const float len = alongX ? b.W() : b.D();
    const int n = std::max(1, static_cast<int>(std::floor(len / width)));
    const float w = len / n;
    for (int i = 0; i < n; ++i) {
      Room room;
      room.r = alongX ? Rect{b.x0 + i * w, b.z0, b.x0 + (i + 1) * w, b.z1} : Rect{b.x0, b.z0 + i * w, b.x1, b.z0 + (i + 1) * w};
      room.side = side;
      room.door = withDoor;
      rooms.push_back(room);
    }
  };
  switch (mode) {
  case Mode::Open:
    open = {{-kHX, kSZ1, kHX, kHZ}, {-kHX, -kHZ, kHX, kSZ0}, {-kHX, kSZ0, kSX0, kSZ1}, {kEX1, kSZ0, kHX, kSZ1}};
    break;
  case Mode::Ring:
    corridors = {{-kHX, kSZ1, kHX, 5.0f}, {-kHX, -5.0f, kHX, kSZ0}, {-8.0f, kSZ0, kSX0, kSZ1}, {kEX1, kSZ0, 6.0f, kSZ1}};
    band({-kHX, 5.0f, kHX, kHZ}, 2, true, uw, true);
    band({-kHX, -kHZ, kHX, -5.0f}, 3, true, uw, true);
    band({-kHX, kSZ0, -8.0f, kSZ1}, 1, false, 6.0f, true);
    band({6.0f, kSZ0, kHX, kSZ1}, 0, false, 6.0f, true);
    break;
  case Mode::Straight:
    corridors = {{-kHX, kSZ1, kHX, 5.0f}};
    band({-kHX, 5.0f, kHX, kHZ}, 2, true, uw, true);
    band({-kHX, -4.0f, kSX0, kSZ1}, 3, true, uw, true);
    band({kEX1, -4.0f, kHX, kSZ1}, 3, true, uw, true);
    // lo de atras: cuartos de servicio cerrados (no se ven)
    band({-kHX, -kHZ, kHX, -4.0f}, 3, true, 12.0f, false);
    band({kSX0, -4.0f, kEX1, kSZ0}, 3, true, 12.0f, false);
    break;
  case Mode::Lobby:
    open = {{-kHX, -kHZ, kHX, kSZ0}, {-kHX, kSZ0, kSX0, kSZ1}, {kEX1, kSZ0, kHX, kSZ1}, {-kHX, kSZ1, kHX, 5.0f}};
    band({-kHX, 5.0f, kHX, kHZ}, 2, true, 6.0f, true);
    break;
  }
  // numeros y el que esta abierto en este piso
  {
    int k = 0;
    for (Room& room : rooms) {
      if (room.door)
        room.number = floor * 100 + (++k);
    }
    if (mode != Mode::Lobby) {
      if (cfg_.kind == BuildingConfig::Kind::Offices) {
        for (Room& room : rooms)
          room.open = room.door;
      } else if (k > 0) {
        int pick = r.Pick(k), seen = 0;
        for (Room& room : rooms) {
          if (room.door && seen++ == pick)
            room.open = true;
        }
      }
    }
  }

  // --------------------------------------------------------------------- pisos, techos y luces
  const std::string& roomFloor = T.floor;
  const std::string& hallFloor = lobby ? T.lobby : (mode == Mode::Open ? T.floor : T.corridor);
  for (const Rect& c : corridors) {
    m.Flat(hallFloor, c, 0.0f, true, 1.0f);
    m.Flat(T.ceiling, c, kCeil, false, 1.2f);
    const bool alongX = c.W() > c.D();
    const float len = alongX ? c.W() : c.D();
    for (float s = 2.5f; s < len; s += 5.0f) {
      const float x = alongX ? c.x0 + s : (c.x0 + c.x1) * 0.5f, z = alongX ? (c.z0 + c.z1) * 0.5f : c.z0 + s;
      panel(x, z);
      if (std::fmod(s, 10.0f) < 5.0f)
        light(x, z);
    }
  }
  for (const Rect& o : open) {
    m.Flat(hallFloor, o, 0.0f, true, lobby ? 2.0f : 1.0f);
    m.Flat(T.ceiling, o, kCeil, false, 1.2f);
    for (float x = o.x0 + 2.0f; x < o.x1 - 1.0f; x += 4.0f) {
      for (float z = o.z0 + 1.5f; z < o.z1 - 0.5f; z += 3.0f) {
        panel(x, z);
        if ((static_cast<int>(x) + static_cast<int>(z)) % 3 == 0)
          light(x, z);
      }
    }
  }
  for (const Room& room : rooms) {
    if (!room.open)
      continue;
    m.Flat(roomFloor, room.r, 0.0f, true, 1.0f);
    m.Flat(T.ceiling, room.r, kCeil, false, 1.2f);
    const float cx = (room.r.x0 + room.r.x1) * 0.5f, cz = (room.r.z0 + room.r.z1) * 0.5f;
    panel(cx, cz);
    light(cx, cz);
  }
  // el piso de toda la planta, para pisar (sin el cubo de la escalera)
  m.Solid({-kHX, -0.5f, -kHZ}, {kHX, 0.0f, kSZ0});
  m.Solid({-kHX, -0.5f, kSZ1}, {kHX, 0.0f, kHZ});
  m.Solid({-kHX, -0.5f, kSZ0}, {kSX0, 0.0f, kSZ1});
  m.Solid({kSX1, -0.5f, kSZ0}, {kHX, 0.0f, kSZ1});

  // --------------------------------------------------------------------- los muros de fuera, con ventanas
  // (de 0.9 a 2.6 m la ventana: la vista; parteluces cada 1.5 m)
  auto exterior = [&](bool alongX, float at, float a0, float a1, bool facingPlus, float holeA, float holeB) {
    auto box = [&](float s0, float s1, float y0, float y1) {
      if (s1 <= s0)
        return;
      const float w0 = at, w1 = at + (facingPlus ? -kExt : kExt);
      const auto [a, b] = alongX ? Sorted({s0, y0, w0}, {s1, y1, w1}) : Sorted({w0, y0, s0}, {w1, y1, s1});
      m.Cube(T.wall, a, b, 3.0f, Mesher::SIDES | Mesher::YP);
    };
    auto pieces = [&](float s0, float s1) {
      box(s0, s1, 0.0f, 0.9f);
      box(s0, s1, 2.6f, kCeil + 0.4f);
      for (float s = s0; s < s1 - 0.05f; s += 1.5f)
        box(s, std::min(s1, s + 0.1f), 0.9f, 2.6f);
      // la vista, dentro del muro
      const float w = at + (facingPlus ? -kExt * 0.5f : kExt * 0.5f);
      if (alongX)
        m.WallZ(T.window, w, s0, s1, 0.9f, 2.6f, facingPlus, 6.0f, true);
      else
        m.WallX(T.window, w, s0, s1, 0.9f, 2.6f, facingPlus, 6.0f, true);
    };
    if (holeB > holeA) {
      pieces(a0, holeA);
      pieces(holeB, a1);
      box(holeA, holeB, 2.7f, kCeil + 0.4f); // encima de la puerta
    } else {
      pieces(a0, a1);
    }
    const auto [a, b] = alongX ? Sorted({a0, 0.0f, at}, {a1, kCeil, at + (facingPlus ? -kExt : kExt)})
                               : Sorted({at, 0.0f, a0}, {at + (facingPlus ? -kExt : kExt), kCeil, a1});
    if (holeB > holeA) {
      // la colision, con el hueco de la puerta (la puerta misma estorba)
      if (alongX) {
        m.Solid(a, {holeA, b.y, b.z});
        m.Solid({holeB, a.y, a.z}, b);
      }
    } else {
      m.Solid(a, b);
    }
  };
  exterior(true, kHZ, -kHX - kExt, kHX + kExt, false, 0, 0);
  exterior(true, -kHZ, -kHX - kExt, kHX + kExt, true, lobby ? -1.2f : 0.0f, lobby ? 1.2f : 0.0f);
  exterior(false, kHX, -kHZ, kHZ, false, 0, 0);
  exterior(false, -kHX, -kHZ, kHZ, true, 0, 0);

  // --------------------------------------------------------------------- los cuartos
  for (const Room& room : rooms) {
    const Rect& q = room.r;
    for (int side = 0; side < 4; ++side) {
      // los lados que caen en el muro de fuera ya estan
      if ((side == 0 && q.x0 <= -kHX + 0.01f) || (side == 1 && q.x1 >= kHX - 0.01f) ||
          (side == 2 && q.z0 <= -kHZ + 0.01f) || (side == 3 && q.z1 >= kHZ - 0.01f))
        continue;
      const bool alongX = side >= 2;
      const float at = side == 0 ? q.x0 : side == 1 ? q.x1 : side == 2 ? q.z0 : q.z1;
      const float inward = (side == 0 || side == 2) ? kPart : -kPart; // el muro va por dentro del cuarto
      const float a0 = alongX ? q.x0 : q.z0, a1 = alongX ? q.x1 : q.z1;
      const bool front = side == room.side && room.door;
      const bool glass = front && cfg_.kind == BuildingConfig::Kind::Offices && mode != Mode::Lobby;
      const float dc = a0 + std::min(1.1f, (a1 - a0) * 0.5f); // el centro de la puerta
      const float d0 = dc - kDoorW * 0.5f, d1 = dc + kDoorW * 0.5f;
      auto piece = [&](float s0, float s1, float y0, float y1, const std::string& tex, bool collide) {
        if (s1 - s0 < 0.01f)
          return;
        const auto [a, b] = alongX ? Sorted({s0, y0, at}, {s1, y1, at + inward}) : Sorted({at, y0, s0}, {at + inward, y1, s1});
        m.Cube(tex, a, b, 3.0f, Mesher::SIDES | Mesher::YP, false, collide);
      };
      const std::string& outside = T.corridorWall;
      if (glass) {
        // frente de vidrio: zoclo, canceleria cada 1.2 m y el cabezal; la puerta abierta
        piece(a0, d0, 0.0f, 0.12f, T.metal, true);
        piece(d1, a1, 0.0f, 0.12f, T.metal, true);
        piece(a0, a1, 2.4f, kCeil, outside, false);
        for (float s = a0; s < a1; s += 1.2f) {
          if (s > d0 - 0.05f && s < d1 + 0.05f)
            continue;
          piece(s, std::min(a1, s + 0.06f), 0.0f, 2.4f, T.metal, true);
        }
        piece(d0 - 0.06f, d0, 0.0f, 2.4f, T.metal, true);
        piece(d1, d1 + 0.06f, 0.0f, 2.4f, T.metal, true);
        // lo que estorba del vidrio (se ve a traves pero no se pasa)
        if (alongX) {
          m.Solid(Sorted({a0, 0, at}, {d0, 2.4f, at + inward}).first, Sorted({a0, 0, at}, {d0, 2.4f, at + inward}).second);
          m.Solid(Sorted({d1, 0, at}, {a1, 2.4f, at + inward}).first, Sorted({d1, 0, at}, {a1, 2.4f, at + inward}).second);
        } else {
          m.Solid(Sorted({at, 0, a0}, {at + inward, 2.4f, d0}).first, Sorted({at, 0, a0}, {at + inward, 2.4f, d0}).second);
          m.Solid(Sorted({at, 0, d1}, {at + inward, 2.4f, a1}).first, Sorted({at, 0, d1}, {at + inward, 2.4f, a1}).second);
        }
        continue;
      }
      if (front) {
        piece(a0, d0, 0.0f, kCeil, outside, true);
        piece(d1, a1, 0.0f, kCeil, outside, true);
        piece(d0, d1, kDoorH, kCeil, outside, false);
        // el marco, la puerta (cerrada, o nada si esta abierta) y su numero
        piece(d0 - 0.06f, d0, 0.0f, kDoorH + 0.06f, T.wood, false);
        piece(d1, d1 + 0.06f, 0.0f, kDoorH + 0.06f, T.wood, false);
        piece(d0, d1, kDoorH, kDoorH + 0.06f, T.wood, false);
        if (!room.open) {
          const float mid = at + inward * 0.5f;
          const auto [a, b] = alongX ? Sorted({d0, 0.0f, mid - 0.025f}, {d1, kDoorH, mid + 0.025f})
                                     : Sorted({mid - 0.025f, 0.0f, d0}, {mid + 0.025f, kDoorH, d1});
          m.Cube(T.wood, a, b, 1.0f, Mesher::ALL, false, true);
        }
        if (room.number > 0 && cfg_.kind != BuildingConfig::Kind::Offices) {
          const float sgn = (side == 0 || side == 2) ? -1.0f : 1.0f; // hacia el pasillo
          const Vector3 n = alongX ? Vector3{0, 0, sgn} : Vector3{sgn, 0, 0};
          const float s = d1 + 0.35f;
          const Vector3 c = alongX ? Vector3{s, 1.55f, at + sgn * 0.005f} : Vector3{at + sgn * 0.005f, 1.55f, s};
          plate(std::to_string(room.number), c, n, 0.09f);
        }
      } else {
        piece(a0, a1, 0.0f, kCeil, room.open ? T.wall : outside, true);
      }
    }
  }

  // --------------------------------------------------------------------- muebles (lo que se ve)
  // Un cuarto se amuebla en sus propios ejes: 'a' a lo ancho de la puerta (-W/2..W/2), 'b' de la
  // puerta (0) al fondo (D), 'h' arriba.
  struct Local {
    Vector3 o, u, v;
    float W, D;
  };
  auto localOf = [&](const Room& room) {
    const Rect& q = room.r;
    switch (room.side) {
    case 0: return Local{{q.x0, 0, (q.z0 + q.z1) * 0.5f}, {0, 0, -1}, {1, 0, 0}, q.D(), q.W()};
    case 1: return Local{{q.x1, 0, (q.z0 + q.z1) * 0.5f}, {0, 0, 1}, {-1, 0, 0}, q.D(), q.W()};
    case 2: return Local{{(q.x0 + q.x1) * 0.5f, 0, q.z0}, {1, 0, 0}, {0, 0, 1}, q.W(), q.D()};
    default: return Local{{(q.x0 + q.x1) * 0.5f, 0, q.z1}, {-1, 0, 0}, {0, 0, -1}, q.W(), q.D()};
    }
  };
  auto at = [](const Local& L, float a, float b, float h) {
    return Vector3Add(L.o, Vector3Add(Vector3Scale(L.u, a), Vector3Add(Vector3Scale(L.v, b), Vector3{0, h, 0})));
  };
  auto fbox = [&](const Local& L, const std::string& tex, float a0, float a1, float b0, float b1, float h0, float h1,
                  bool collide = true, float tile = 1.0f) {
    const auto [p, q] = Sorted(at(L, a0, b0, h0), at(L, a1, b1, h1));
    m.Cube(tex, p, q, tile, Mesher::NOBOTTOM, false, collide);
  };
  auto glowBox = [&](const Local& L, const std::string& tex, float a0, float a1, float b0, float b1, float h0, float h1) {
    const auto [p, q] = Sorted(at(L, a0, b0, h0), at(L, a1, b1, h1));
    m.Cube(tex, p, q, 1.0f, Mesher::NOBOTTOM, true);
  };
  auto desk = [&](const Local& L, float a, float b, bool facingDoor) {
    const float s = facingDoor ? -1.0f : 1.0f; // de que lado se sienta
    fbox(L, T.counter, a - 0.8f, a + 0.8f, b - 0.4f, b + 0.4f, 0.7f, 0.75f, true, 2.0f);
    fbox(L, T.counter, a - 0.8f, a - 0.75f, b - 0.4f, b + 0.4f, 0.0f, 0.7f, false);
    fbox(L, T.counter, a + 0.75f, a + 0.8f, b - 0.4f, b + 0.4f, 0.0f, 0.7f, false);
    // el monitor y su pantalla
    fbox(L, T.metal, a - 0.3f, a + 0.3f, b + s * 0.28f, b + s * 0.25f, 0.95f, 1.3f, false);
    fbox(L, T.metal, a - 0.04f, a + 0.04f, b + s * 0.3f, b + s * 0.26f, 0.75f, 0.95f, false);
    glowBox(L, T.screen, a - 0.27f, a + 0.27f, b + s * 0.245f, b + s * 0.24f, 0.98f, 1.27f);
    // la silla
    const float cb = b - s * 0.75f;
    fbox(L, T.fabric, a - 0.25f, a + 0.25f, cb - 0.25f, cb + 0.25f, 0.42f, 0.5f, true);
    fbox(L, T.fabric, a - 0.25f, a + 0.25f, cb - s * 0.25f, cb - s * 0.2f, 0.5f, 1.05f, false);
    fbox(L, T.metal, a - 0.03f, a + 0.03f, cb - 0.03f, cb + 0.03f, 0.0f, 0.42f, false);
  };
  auto plant = [&](const Local& L, float a, float b) {
    fbox(L, T.concrete, a - 0.22f, a + 0.22f, b - 0.22f, b + 0.22f, 0.0f, 0.45f, true);
    fbox(L, T.plant, a - 0.35f, a + 0.35f, b - 0.35f, b + 0.35f, 0.45f, 1.3f, false, 0.7f);
    fbox(L, T.plant, a - 0.2f, a + 0.2f, b - 0.2f, b + 0.2f, 1.3f, 1.7f, false, 0.7f);
  };
  auto sofa = [&](const Local& L, float a, float b, float w, const std::string& tex) {
    fbox(L, tex, a - w * 0.5f, a + w * 0.5f, b - 0.45f, b + 0.45f, 0.0f, 0.42f, true, 1.0f);
    fbox(L, tex, a - w * 0.5f, a + w * 0.5f, b + 0.25f, b + 0.45f, 0.42f, 0.85f, false);
    fbox(L, tex, a - w * 0.5f, a - w * 0.5f + 0.18f, b - 0.45f, b + 0.25f, 0.42f, 0.62f, false);
    fbox(L, tex, a + w * 0.5f - 0.18f, a + w * 0.5f, b - 0.45f, b + 0.25f, 0.42f, 0.62f, false);
  };
  auto bed = [&](const Local& L, float a, float b, float w, float len) {
    fbox(L, T.wood, a - w * 0.5f, a + w * 0.5f, b - 0.1f, b, 0.0f, 1.1f, false); // cabecera (contra el fondo)
    fbox(L, T.wood, a - w * 0.5f, a + w * 0.5f, b - len, b - 0.1f, 0.0f, 0.35f, true);
    fbox(L, T.bed, a - w * 0.5f + 0.02f, a + w * 0.5f - 0.02f, b - len + 0.02f, b - 0.12f, 0.35f, 0.55f, false, 2.0f);
    fbox(L, T.fabric2, a - w * 0.5f + 0.1f, a + w * 0.5f - 0.1f, b - 0.6f, b - 0.15f, 0.55f, 0.68f, false);
  };
  auto nightstand = [&](const Local& L, float a, float b) {
    fbox(L, T.wood, a - 0.25f, a + 0.25f, b - 0.4f, b, 0.0f, 0.55f, true);
    fbox(L, T.metal, a - 0.04f, a + 0.04f, b - 0.24f, b - 0.16f, 0.55f, 0.85f, false);
    glowBox(L, T.light, a - 0.16f, a + 0.16f, b - 0.36f, b - 0.04f, 0.85f, 1.05f);
  };

  for (const Room& room : rooms) {
    if (!room.open)
      continue;
    const Local L = localOf(room);
    const float W = L.W, D = L.D;
    const float hw = W * 0.5f - kPart;
    switch (cfg_.kind) {
    case BuildingConfig::Kind::Offices: {
      if (room.number % 3 == 0 && W > 5.0f) { // sala de juntas
        fbox(L, T.counter, -W * 0.3f, W * 0.3f, D * 0.5f - 0.6f, D * 0.5f + 0.6f, 0.72f, 0.76f, true, 2.0f);
        fbox(L, T.metal, -W * 0.25f, W * 0.25f, D * 0.5f - 0.3f, D * 0.5f + 0.3f, 0.0f, 0.72f, false);
        for (float a = -W * 0.25f; a <= W * 0.25f + 0.01f; a += 1.0f) {
          for (const float sgn : {-1.0f, 1.0f})
            fbox(L, T.fabric, a - 0.22f, a + 0.22f, D * 0.5f + sgn * 0.95f - 0.22f, D * 0.5f + sgn * 0.95f + 0.22f, 0.0f, 0.9f, true);
        }
      } else {
        desk(L, 0.0f, D * 0.62f, true);
        fbox(L, T.wood, -hw, -hw + 0.4f, D * 0.3f, D * 0.85f, 0.0f, 1.9f, true, 1.0f); // el librero
        plant(L, hw - 0.4f, D - 0.6f);
      }
      break;
    }
    case BuildingConfig::Kind::Apartments: {
      // cocina contra un lado, la sala en medio, la cama al fondo
      fbox(L, T.kitchen, -hw, -hw + 0.6f, 0.9f, std::min(D * 0.5f, 3.6f), 0.0f, 0.9f, true, 1.2f);
      fbox(L, T.counter, -hw, -hw + 0.62f, 0.9f, std::min(D * 0.5f, 3.6f), 0.9f, 0.94f, false, 2.0f);
      fbox(L, T.kitchen, -hw, -hw + 0.35f, 0.9f, std::min(D * 0.5f, 3.6f), 1.5f, 2.2f, false, 1.2f);
      fbox(L, T.metal, -hw, -hw + 0.7f, std::min(D * 0.5f, 3.6f), std::min(D * 0.5f, 3.6f) + 0.7f, 0.0f, 1.85f, true);
      sofa(L, hw * 0.25f, D * 0.42f, std::min(2.2f, W * 0.4f), T.fabric);
      fbox(L, T.rug, hw * 0.25f - 1.2f, hw * 0.25f + 1.2f, D * 0.42f - 1.9f, D * 0.42f - 0.6f, 0.0f, 0.01f, false, 2.4f);
      fbox(L, T.wood, hw * 0.25f - 0.5f, hw * 0.25f + 0.5f, D * 0.42f - 1.5f, D * 0.42f - 1.0f, 0.0f, 0.4f, true);
      fbox(L, T.wood, hw - 0.45f, hw, D * 0.2f, D * 0.2f + 1.4f, 0.0f, 0.5f, true); // el mueble de la tele
      fbox(L, T.metal, hw - 0.12f, hw - 0.06f, D * 0.2f + 0.1f, D * 0.2f + 1.3f, 0.5f, 1.2f, false);
      glowBox(L, T.screen, hw - 0.13f, hw - 0.125f, D * 0.2f + 0.15f, D * 0.2f + 1.25f, 0.55f, 1.15f);
      bed(L, hw * 0.3f, D - kPart, 1.5f, 2.0f);
      nightstand(L, hw * 0.3f - 1.1f, D - kPart);
      fbox(L, T.wood, -hw, -hw + 0.6f, D - 2.2f, D - kPart, 0.0f, 2.1f, true); // el ropero
      break;
    }
    case BuildingConfig::Kind::Hotel: {
      const float bw = W > 5.0f ? 1.9f : 1.5f;
      bed(L, 0.4f, D - kPart, bw, 2.0f);
      nightstand(L, 0.4f - bw * 0.5f - 0.35f, D - kPart);
      nightstand(L, 0.4f + bw * 0.5f + 0.35f, D - kPart);
      fbox(L, T.counter, -hw, -hw + 0.55f, D * 0.35f, D * 0.35f + 1.4f, 0.72f, 0.76f, true, 2.0f); // escritorio
      fbox(L, T.wood, -hw, -hw + 0.55f, D * 0.35f, D * 0.35f + 0.05f, 0.0f, 0.72f, false);
      fbox(L, T.wood, -hw, -hw + 0.55f, D * 0.35f + 1.35f, D * 0.35f + 1.4f, 0.0f, 0.72f, false);
      fbox(L, T.metal, -hw + 0.05f, -hw + 0.1f, D * 0.35f + 0.2f, D * 0.35f + 1.2f, 1.2f, 1.75f, false);
      glowBox(L, T.screen, -hw + 0.1f, -hw + 0.105f, D * 0.35f + 0.25f, D * 0.35f + 1.15f, 1.24f, 1.71f);
      fbox(L, T.fabric, -hw + 0.7f, -hw + 1.15f, D * 0.35f + 0.5f, D * 0.35f + 0.95f, 0.0f, 0.9f, true);
      // el bano: una caja con su puerta, junto a la entrada
      fbox(L, T.wall, hw - 1.8f, hw, 0.0f, 2.2f, 0.0f, 2.5f, true, 1.5f);
      fbox(L, T.wood, hw - 1.75f, hw - 0.85f, 2.2f, 2.23f, 0.0f, 2.05f, false);
      if (W > 5.0f)
        sofa(L, -hw + 1.4f, D - 2.6f, 1.6f, T.fabric2);
      fbox(L, T.rug, -1.0f, 1.8f, D - 3.4f, D - 2.2f, 0.0f, 0.01f, false, 2.4f);
      break;
    }
    }
  }

  // la planta abierta de oficinas: islas de cuatro escritorios con mamparas
  if (mode == Mode::Open) {
    for (const Rect& o : open) {
      for (float x = o.x0 + 2.4f; x < o.x1 - 2.4f; x += 4.4f) {
        for (float z = o.z0 + 2.2f; z < o.z1 - 2.2f; z += 4.2f) {
          const Local L{{x, 0, z}, {1, 0, 0}, {0, 0, 1}, 4.0f, 0.0f};
          fbox(L, T.partition, -1.7f, 1.7f, -0.03f, 0.03f, 0.0f, 1.25f, true, 1.2f);
          fbox(L, T.partition, -0.03f, 0.03f, -1.6f, 1.6f, 0.0f, 1.25f, true, 1.2f);
          for (const float a : {-0.85f, 0.85f}) {
            desk(L, a, 0.5f, false);
            desk(L, a, -0.5f, true);
          }
        }
      }
    }
  }

  // --------------------------------------------------------------------- el vestibulo (planta baja)
  if (lobby) {
    const Local L{{0, 0, -kHZ}, {1, 0, 0}, {0, 0, 1}, 36.0f, 9.0f};
    m.Cube(T.rug, {-2.4f, 0.0f, -kHZ + 0.5f}, {2.4f, 0.012f, -kHZ + 6.0f}, 2.4f, Mesher::YP);
    switch (cfg_.kind) {
    case BuildingConfig::Kind::Offices:
      fbox(L, T.counter, 4.0f, 9.0f, 5.0f, 5.8f, 0.0f, 1.1f, true, 2.0f); // recepcion
      for (float a = -6.0f; a <= -2.0f; a += 1.0f)                       // los torniquetes
        fbox(L, T.metal, a - 0.1f, a + 0.1f, 7.0f, 8.0f, 0.0f, 1.0f, true);
      sofa(L, -12.0f, 3.0f, 2.4f, T.fabric);
      plant(L, -14.5f, 1.0f);
      plant(L, 14.5f, 1.0f);
      break;
    case BuildingConfig::Kind::Apartments:
      fbox(L, T.mail, -14.0f, -6.0f, 8.5f, 8.9f, 0.0f, 1.9f, true, 2.0f); // los buzones contra el nucleo
      fbox(L, T.wood, 8.0f, 11.0f, 3.0f, 3.5f, 0.0f, 0.45f, true);       // una banca
      plant(L, 12.0f, 3.25f);
      break;
    case BuildingConfig::Kind::Hotel:
      fbox(L, T.counter, -9.0f, -2.5f, 6.5f, 7.4f, 0.0f, 1.1f, true, 2.0f);
      fbox(L, T.wood, -9.0f, -2.5f, 8.4f, 8.8f, 0.0f, 2.6f, true, 1.0f);
      sofa(L, 7.0f, 3.0f, 2.4f, T.fabric);
      sofa(L, 11.0f, 3.0f, 2.4f, T.fabric);
      fbox(L, T.wood, 8.0f, 10.0f, 1.2f, 2.0f, 0.0f, 0.42f, true);
      plant(L, 14.5f, 1.0f);
      plant(L, -14.5f, 1.0f);
      break;
    }
  }

  // --------------------------------------------------------------------- el nucleo: elevadores
  m.Cube(T.corridorWall, {kEX0, 0.0f, kSZ0}, {kEX1, kCeil, kSZ1}, 3.0f, Mesher::SIDES, false, true);
  for (const float x : {-0.6f, 2.2f}) {
    m.Cube(T.metal, {x - 0.8f, 0.0f, kSZ1}, {x + 0.8f, 2.35f, kSZ1 + 0.04f}, 0.5f, Mesher::SIDES | Mesher::YP);
    m.Cube(T.metal, {x - 0.68f, 0.0f, kSZ1 + 0.04f}, {x - 0.01f, 2.2f, kSZ1 + 0.06f}, 1.0f, Mesher::ZP | Mesher::XN | Mesher::XP);
    m.Cube(T.metal, {x + 0.01f, 0.0f, kSZ1 + 0.04f}, {x + 0.68f, 2.2f, kSZ1 + 0.06f}, 1.0f, Mesher::ZP | Mesher::XN | Mesher::XP);
    // el piso donde dice que esta (nunca llega)
    plate(std::to_string(r.Pick(top + 1)), {x, 2.6f, kSZ1 + 0.01f}, {0, 0, 1}, 0.12f);
  }
  m.Cube(T.metal, {0.7f, 1.0f, kSZ1}, {0.9f, 1.4f, kSZ1 + 0.03f}, 0.5f); // los botones
  // del otro lado, los banos
  for (const float x : {-0.5f, 2.5f})
    m.Cube(T.wood, {x - 0.45f, 0.0f, kSZ0 - 0.04f}, {x + 0.45f, kDoorH, kSZ0}, 1.0f, Mesher::SIDES | Mesher::YP);

  // --------------------------------------------------------------------- el cubo de la escalera
  // Niveles relativos a este piso: k = -1 (si hay), 0 y 1; los descansos de norte de k = lo..hi (el de
  // arriba del ultimo piso es el de la azotea, con su puerta y su techo).
  const int lo = floor > 0 ? -1 : 0;
  const int hi = std::min(2, top + 1 - floor);
  const bool roofInView = floor + hi == top + 1;
  const float yBottom = lo * H - 0.3f;
  const float yTop = hi * H + (roofInView ? 3.0f : 0.0f);
  m.Cube(T.concrete, {kSX0, yBottom, kSZ0}, {kIX0, yTop, kSZ1}, 2.0f, Mesher::SIDES, false, true);
  m.Cube(T.concrete, {kIX1, yBottom, kSZ0}, {kSX1, yTop, kSZ1}, 2.0f, Mesher::SIDES, false, true);
  m.Cube(T.concrete, {kIX0, yBottom, kSZ0}, {kIX1, yTop, kIZ0}, 2.0f, Mesher::SIDES, false, true);
  // el muro norte, con el hueco de la puerta de cada descanso (menos el de hasta arriba si no es la azotea)
  float y = yBottom;
  for (int k = lo; k <= hi; ++k) {
    const float base = k * H;
    const bool roofDoor = floor + k == top + 1;
    if (k == hi && !roofDoor)
      break;
    m.Cube(T.concrete, {kIX0, y, kIZ1}, {kIX1, base, kSZ1}, 2.0f, Mesher::SIDES, false, true);
    m.Cube(T.concrete, {kIX0, base, kIZ1}, {kStairDoor - kDoorW * 0.5f, base + kDoorH, kSZ1}, 2.0f, Mesher::SIDES, false, true);
    m.Cube(T.concrete, {kStairDoor + kDoorW * 0.5f, base, kIZ1}, {kIX1, base + kDoorH, kSZ1}, 2.0f, Mesher::SIDES, false, true);
    y = base + kDoorH;
    // las puertas de los otros niveles: de lamina, cerradas (la de este piso y la de la azotea son de verdad)
    if (!(k == 0 || (roofDoor && k == 1)))
      m.Cube(T.metal, {kStairDoor - kDoorW * 0.5f, base, kIZ1 + 0.05f}, {kStairDoor + kDoorW * 0.5f, base + kDoorH, kSZ1 - 0.05f},
             1.0f, Mesher::ALL, false, true);
    if (!roofDoor)
      plate(std::to_string(floor + k), {kStairDoor - 1.2f, base + 1.6f, kIZ1 - 0.005f}, {0, 0, -1}, 0.22f);
  }
  m.Cube(T.concrete, {kIX0, y, kIZ1}, {kIX1, yTop, kSZ1}, 2.0f, Mesher::SIDES, false, true);
  m.Flat(T.concrete, {kIX0, kIZ0, kIX1, kIZ1}, yTop, false, 2.0f);
  m.Flat(T.concrete, {kIX0, kIZ0, kIX1, kIZ1}, yBottom + 0.29f, true, 2.0f);
  if (floor == 0)
    m.Solid({kIX0, -0.5f, kIZ0}, {kIX1, 0.0f, kIZ1});
  // fuera, del lado del pasillo: el numero de este piso
  plate(std::to_string(floor), {kStairDoor + 1.2f, 1.6f, kSZ1 + 0.005f}, {0, 0, 1}, 0.18f);
  // los descansos de norte
  for (int k = lo; k <= hi; ++k)
    m.Cube(T.concrete, {kIX0, k * H - 0.3f, kIZ1 - kLand}, {kIX1, k * H, kIZ1}, 2.0f, Mesher::ALL, false, true);

  // los tramos: A (oeste) sube hacia el sur de k*H a k*H + H/2; B (este) sube hacia el norte hasta (k+1)*H
  const float rise = H * 0.5f / kRisers;
  const float run = (kIZ1 - kLand) - (kIZ0 + kLand);
  const float tread = run / (kRisers - 1);
  for (int k = lo; k <= 1; ++k) {
    if (floor + k < 0 || floor + k > top)
      continue; // no hay tramo de ese nivel al siguiente
    const float base = k * H;
    const float ym = base + H * 0.5f;
    m.Cube(T.concrete, {kIX0, ym - 0.3f, kIZ0}, {kIX1, ym, kIZ0 + kLand}, 2.0f, Mesher::ALL, false, true);
    for (int i = 1; i < kRisers; ++i) {
      const float za1 = (kIZ1 - kLand) - (i - 1) * tread, za0 = za1 - tread;
      const float ya = base + i * rise;
      m.Cube(T.concrete, {kIX0, ya - 0.45f, za0}, {kMid - 0.05f, ya, za1}, 1.0f, Mesher::ALL, false, true);
      const float zb0 = (kIZ0 + kLand) + (i - 1) * tread, zb1 = zb0 + tread;
      const float yb = ym + i * rise;
      m.Cube(T.concrete, {kMid + 0.05f, yb - 0.45f, zb0}, {kIX1, yb, zb1}, 1.0f, Mesher::ALL, false, true);
    }
    // el muro de en medio, entre los dos tramos
    m.Cube(T.concrete, {kMid - 0.05f, base - 0.45f, kIZ0 + kLand}, {kMid + 0.05f, base + H, kIZ1 - kLand}, 2.0f, Mesher::ALL,
           false, true);
    // una lampara en cada descanso
    m.Cube(T.light, {kStairDoor - 0.3f, base + 2.9f, kIZ1 - 0.25f}, {kStairDoor + 0.3f, base + 3.0f, kIZ1 - 0.15f}, 1.0f,
           Mesher::ALL, true);
    m.Cube(T.light, {kStairDoor - 0.3f, ym + 2.9f, kIZ0 + 0.15f}, {kStairDoor + 0.3f, ym + 3.0f, kIZ0 + 0.25f}, 1.0f,
           Mesher::ALL, true);
    PointLight l;
    l.position = {kStairDoor, base + 2.6f, 0.0f};
    l.color = {0.85f, 0.9f, 0.95f};
    l.radius = 6.0f;
    lights.push_back(l);
  }

  // --------------------------------------------------------------------- subir todo
  scene.streamedLights = lights;
  auto add = [&](std::unique_ptr<Entity> e) {
    entities_.push_back(e->Name());
    scene.Add(std::move(e));
  };
  {
    auto e = std::make_unique<Entity>("~ed:solid", UploadMesh(engine_, textures_, solidData));
    e->colliders = std::move(colliders);
    add(std::move(e));
  }
  if (!glowData.parts.empty()) {
    auto e = std::make_unique<Entity>("~ed:glow", UploadMesh(engine_, textures_, glowData));
    e->emissive = true;
    add(std::move(e));
  }
  auto door = [&](const std::string& name, const std::string& model, Vector3 p, float yaw, const std::string& prompt,
                  std::vector<Action> actions, BoxCollider box) {
    auto res = engine_.Assets().GetModel(model);
    res->SetShader(engine_.Retro().Lit());
    auto e = std::make_unique<Entity>(name, res);
    e->transform.position = p;
    e->transform.rotation = {0.0f, yaw, 0.0f};
    Interaction it;
    it.prompt = prompt;
    it.range = 2.2f;
    it.box = box;
    it.actions = std::move(actions);
    e->interact = std::move(it);
    e->colliders.push_back(box);
    add(std::move(e));
  };
  const BoxCollider small{{-0.5f, 0.0f, -0.03f}, {0.5f, kDoorH, 0.03f}, 0.0f};
  // la puerta de la escalera de este piso: una de cada lado (cada una lleva al otro)
  door("~ed:escalera_dentro", cfg_.stairDoor, {kStairDoor, 0.0f, kIZ1 + 0.04f}, 180.0f, cfg_.openPrompt,
       {action::Teleport{{kStairDoor, 0.0f, kSZ1 + 0.8f}, YawToward(0.0f, 1.0f)}}, small);
  door("~ed:escalera_fuera", cfg_.stairDoor, {kStairDoor, 0.0f, kSZ1 - 0.04f}, 0.0f, cfg_.openPrompt,
       {action::Teleport{{kStairDoor, 0.0f, kIZ1 - 0.5f}, YawToward(0.0f, -1.0f)}}, small);
  if (floor == top) // la azotea
    door("~ed:azotea", cfg_.stairDoor, {kStairDoor, H, kIZ1 + 0.04f}, 180.0f, cfg_.roofPrompt, {action::ToRoof{}}, small);
  if (lobby) // la salida a la calle
    door("~ed:salida", cfg_.exitDoor, {0.0f, 0.0f, -kHZ - kExt * 0.5f}, 0.0f, cfg_.exitPrompt, {action::LeaveBuilding{}},
         {{-1.2f, 0.0f, -0.05f}, {1.2f, 2.7f, 0.05f}, 0.0f});
}

} // namespace retro
