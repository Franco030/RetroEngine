#pragma once

#include "assets/ObjLoader.hpp"
#include "assets/Resources.hpp"
#include "graphics/Light.hpp"
#include "physics/Collision.hpp"

#include <raylib.h>

#include <cstdint>
#include <future>
#include <map>
#include <memory>
#include <string>
#include <utility>
#include <vector>

namespace retro {

class RetroEngine;
class Scene;

// Una ciudad dentro de un cilindro sin fin (Onus Somaticum). El eje del cilindro es Z; la escena la
// pide con "city": { ... } y la ciudad se genera sola alrededor de quien mira, en trozos a lo
// largo del eje: cada trozo sale de su numero (el mismo numero, la misma ciudad), asi que no se
// acaba nunca y nunca se repite igual. Cerca, torres con todo su detalle, farolas, quioscos y
// colisiones para caminar por la avenida de abajo; lejos, bloques. Las ventanas del cilindro
// (franjas de luz a lo largo) siguen un ciclo de dia y noche.
struct CityConfig {
  float radius = 1500.0f;      // del eje a la tierra
  float chunkLength = 240.0f;  // lo que mide un trozo a lo largo
  float block = 120.0f;        // paso de las manzanas, a lo ancho y a lo largo
  int viewChunks = 14;         // trozos que se ven a cada lado
  int detailChunks = 1;        // trozos a cada lado con todo el detalle (y colisiones)
  std::vector<float> lands{0.0f, 120.0f, 240.0f}; // centros (grados) de las franjas de tierra; 0 = abajo
  float landWidth = 96.0f;     // grados de cada franja (el resto, ventanas)
  float avenue = 80.0f;        // la avenida de abajo, sin torres (m)
  float walk = 150.0f;         // hasta donde se camina a los lados de la avenida (m)
  float detailArc = 520.0f;    // a lo ancho, hasta donde llega el detalle (m desde el centro de abajo)
  float minHeight = 220.0f;
  float maxHeight = 780.0f;
  std::uint64_t seed = 1702;
  long long firstSector = 4812031; // el numero del trozo donde se empieza
  std::string sectorLabel = "SECTOR";
  std::string foodLabel = "COMIDA";
  struct Textures {
    std::vector<std::string> facades;  // muchas: cada torre (y cada retranqueo) escoge
    std::vector<std::string> podiums;  // zocalos con comercios
    std::vector<std::string> roofs;
    std::string ground, avenue, windows, neon, glyphs, pod, metal, lamp;
  } textures;
  std::string glyphs = "0123456789-ABCDEFGHIJKLMNOPQRSTUVWXYZ"; // el orden de la rejilla de 'glyphs'
  int glyphCols = 8;
  int glyphRows = 5;
  struct Cycle {
    float seconds = 0.0f; // 0 = siempre de dia
    float start = 0.25f;  // en que punto empieza (0 = mediodia, 0.5 = medianoche)
    Color dayFog{78, 82, 92, 255};
    Color nightFog{14, 15, 22, 255};
    Color dayWindows{236, 236, 228, 255};
    Color nightWindows{40, 46, 66, 255};
    float dayAmbient = 0.6f;
    float nightAmbient = 0.22f;
    float daySun = 0.3f;
    float nightSun = 0.0f;
  } cycle;
  int pods = 3;              // capsulas por trozo
  float podSpeed = 45.0f;    // m/s
  float lampEvery = 40.0f;   // farolas de la avenida, cada tantos metros
};

class CylinderCity {
public:
  CylinderCity(CityConfig config, RetroEngine& engine);
  ~CylinderCity();

  CylinderCity(const CylinderCity&) = delete;
  CylinderCity& operator=(const CylinderCity&) = delete;

  // Pone y quita trozos alrededor de 'focus' y lleva el ciclo. Si 'focus' se ha alejado mucho
  // del origen, recentra el mundo (todo se mueve en Z un numero entero de trozos, sin que se
  // note) y devuelve cuanto: quien llama mueve al jugador y a la camara lo mismo. Si no, 0.
  float Update(Scene& scene, Vector3 focus, float dt);

  // El numero del sector donde esta 'z' (en el mundo de ahora).
  long long SectorAt(float z) const;

  // Lo que la ciudad genera para un trozo (sin GPU: se hace en otro hilo).
  struct ChunkData {
    ObjData solid;
    ObjData glow;
    std::vector<BoxCollider> colliders;
    std::vector<PointLight> lights; // en coordenadas del trozo
    struct Pod {
      Vector3 position;
      float roll;
      float speed;
    };
    std::vector<Pod> pods;
  };

private:
  struct Chunk {
    int lod = -1;
    std::vector<std::string> entities;
    std::vector<PointLight> lights;
  };

  void Place(Scene& scene, long long index, int lod, ChunkData data);
  void Drop(Scene& scene, Chunk& chunk);
  void ApplyCycle(Scene& scene, float dt);
  std::shared_ptr<ModelResource> Upload(const ObjData& data);

  CityConfig cfg_;
  RetroEngine& engine_;
  long long origin_ = 0; // el trozo que esta en z = 0
  std::map<long long, Chunk> chunks_;
  std::map<std::pair<long long, int>, std::future<ChunkData>> pending_;
  std::map<std::string, std::shared_ptr<TextureResource>> textures_;
  std::shared_ptr<ModelResource> podModel_;
  float time_ = 0.0f;
  bool first_ = true;
};

} // namespace retro
