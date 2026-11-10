#pragma once

#include "assets/ObjLoader.hpp"
#include "audio/Emitter.hpp"
#include "assets/Resources.hpp"
#include "graphics/Light.hpp"
#include "physics/Collision.hpp"
#include "world/Logic.hpp"

#include <raylib.h>

#include <cmath>
#include <cstdint>
#include <future>
#include <map>
#include <memory>
#include <string>
#include <tuple>
#include <utility>
#include <vector>

namespace retro {

class RetroEngine;
class Scene;

// Una ciudad dentro de un cilindro sin fin (Onus Somaticum). El eje del cilindro es Z; la escena la
// pide con "city": { ... } y la ciudad se genera sola alrededor de quien mira, en trozos a lo
// largo del eje: cada trozo sale de su numero (el mismo numero, la misma ciudad), asi que no se
// acaba nunca y nunca se repite igual. Cerca, torres con todo su detalle, farolas, quioscos y
// colisiones; lejos, bloques. Las ventanas del cilindro (franjas de luz a lo largo) siguen un ciclo
// de dia y noche.
//
// Se camina por toda la cara de dentro (un cilindro de O'Neill: el suelo siempre es "abajo"). El
// jugador se queda siempre abajo del mundo y es la ciudad la que gira bajo sus pies: 'phi' grados
// alrededor del eje (lo que en la ciudad esta en el angulo theta, en el mundo esta en theta - phi).
// Asi el motor sigue con Y hacia arriba y cajas de colision derechas: cerca del jugador todo esta
// casi derecho.
struct CityConfig {
  float radius = 1500.0f;      // del eje a la tierra
  float chunkLength = 240.0f;  // lo que mide un trozo a lo largo
  float block = 120.0f;        // paso de las manzanas, a lo ancho y a lo largo
  int viewChunks = 14;         // trozos que se ven a cada lado
  int detailChunks = 1;        // trozos a cada lado con todo el detalle (y colisiones)
  std::vector<float> lands{0.0f, 120.0f, 240.0f}; // centros (grados) de las franjas de tierra; 0 = abajo
  float landWidth = 96.0f;     // grados de cada franja (el resto, ventanas)
  float avenue = 80.0f;        // la avenida de abajo, sin torres (m)
  float walk = 150.0f;         // a lo ancho, hasta donde hay colisiones alrededor de quien anda (m)
  float detailArc = 520.0f;    // a lo ancho, hasta donde llega el detalle alrededor de quien anda (m)
  float detailStep = 120.0f;   // cada tantos metros a lo ancho se rehace el detalle (y las colisiones)
  float minHeight = 220.0f;
  float maxHeight = 780.0f;
  std::uint64_t seed = 1702;
  long long firstSector = 4812031; // el numero del trozo donde se empieza
  std::string sectorLabel = "SECTOR";
  struct Textures {
    std::vector<std::string> facades;  // muchas: cada torre (y cada retranqueo) escoge
    std::vector<std::string> podiums;  // zocalos con comercios
    std::vector<std::string> roofs;
    // el pavimento de lo que se camina (losas de tamano real, una al azar por losa de 'paveTile' m)
    // y la franja de en medio de la avenida
    std::vector<std::string> pavement;
    std::string pavementLine;
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
  float paveTile = 6.0f;     // lo que mide cada losa del pavimento (m)
  // Los locales del pie de las torres de cerca, en 3D (los de lejos llevan el zocalo pintado):
  // aparadores hundidos con su local con luz detras, cortinas de acero, locales tapiados o vacios,
  // pilastras, cajas de luz con garabatos, toldos y el escalon. Arriba sigue el zocalo con sus oficinas
  // ('tops': uno por cada textura de 'podiums', en el mismo orden). Sin 'shops', todo pintado.
  struct Storefronts {
    float arc = 260.0f; // a lo ancho, hasta donde (m desde quien anda)
    std::vector<std::string> tops;
    std::vector<std::string> shops;    // el fondo de los locales con luz
    std::vector<std::string> shutters; // cortinas de acero
    std::vector<std::string> awnings;  // lonas de toldo
    std::vector<std::string> signs;    // cajas de luz (garabatos)
    std::vector<std::string> lobbies;  // el vestibulo de la torre, visto por su puerta
    std::string boards, dark, pillar, frame, floor, wall, ceiling;
    bool Enabled(std::size_t podiums) const { return !shops.empty() && tops.size() == podiums; }
  } storefronts;
  // Por dentro de las torres de cerca (las que llevan locales en 3D): la escena de cada tipo, tres
  // variantes; la puerta del vestibulo (vidrio) y la de la caseta de la azotea (lamina). Cada torre
  // escoge tipo y variante con su semilla. Sin escenas, no se entra.
  struct Interiors {
    std::vector<std::string> offices, apartments, hotel;
    std::string lobbyDoor, roofDoor;
    std::string enterPrompt = "Entrar";
    std::string downPrompt = "Bajar";
    bool Enabled() const { return !offices.empty() && !apartments.empty() && !hotel.empty() && !lobbyDoor.empty(); }
  } interiors;
  // La tienda de COMIDA de la avenida: se entra. Una cada 'everySectors' sectores, de un lado o del otro. Por
  // dentro, gondolas, refrigeradores, mostrador, carteles; su musica suena dentro y en la puerta,
  // y la campanita al cruzar la puerta.
  struct Store {
    bool enabled = false;
    int everySectors = 10;
    // El modelo 3D (tools/gen_onus_tienda_modelo.py): lo solido y lo que brilla (lamparas, el fondo
    // de los refrigeradores, el letrero), en variantes; cada tienda escoge una. Ejes del modelo: X hacia
    // dentro (0 = la fachada), Y arriba, Z a lo largo del frente; 12 x 9 x 4.2 m.
    struct Variant {
      std::string model;
      std::string glow;
    };
    std::vector<Variant> variants;
    // El letrero de fuera y las placas de dentro: garabatos en relieve que parecen letras (nada que
    // traducir en el modelo). Con E se lee lo que dicen ('*Message', que si se traduce).
    std::string signPrompt = "Leer el letrero";
    std::string signMessage;
    std::vector<std::string> plaques; // modelos de placa (el frente mira a -X, el origen al centro)
    std::string plaquePrompt = "Leer";
    std::vector<std::string> plaqueMessages;
    std::string music;
    float musicVolume = 0.8f;
    std::string chime;
    float chimeVolume = 0.7f;
    // los pasos de dentro (el mosaico): mientras se esta dentro, en lugar de los de la escena
    std::vector<std::string> footsteps;
    float footstepVolume = 0.6f;
  } store;
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

  // Lo que el mundo se recentro en Update: 'dz' en Z y 'turn' grados alrededor del eje Z. Quien
  // lleva al jugador lo mueve igual (primero gira (x, y) 'turn' grados, luego resta 'dz').
  struct Recenter {
    float dz = 0.0f;
    float turn = 0.0f;
  };

  // Pone y quita trozos alrededor de 'focus' y lleva el ciclo. Si 'focus' se ha alejado mucho
  // del origen, recentra el mundo (todo se mueve en Z un numero entero de trozos, sin que se
  // note). Con 'carried' (el foco es el jugador), ademas gira la ciudad para que quede abajo.
  Recenter Update(Scene& scene, Vector3 focus, float dt, bool carried);

  // Si 'focus' esta dentro de una tienda (lo de la ultima vez que se llamo a Update).
  bool Indoors() const noexcept { return indoors_; }
  // Donde esta quien anda, alrededor del cilindro (grados; 0 = la avenida).
  double Phi() const noexcept { return phi_; }
  // El trozo que esta en z = 0 (con Phi y la posicion del jugador: el lugar exacto, para guardar).
  long long Origin() const noexcept { return origin_; }
  // Antes del primer Update: empezar en ese lugar (una partida guardada).
  struct Spot {
    long long origin = 0;
    double phi = 0.0;
  };
  void StartAt(Spot place) noexcept {
    origin_ = place.origin;
    phi_ = place.phi;
    anchor_ = std::llround(phi_ * PI / 180.0 * cfg_.radius / cfg_.detailStep);
  }

  // El numero del sector donde esta 'z' (en el mundo de ahora).
  long long SectorAt(float z) const;

  // Una caja de colision de la ciudad: derecha en un mundo girado 'theta' grados (lo que en la
  // ciudad esta en 'theta', alli esta abajo). Con 'phi' se lleva al mundo: su centro gira (theta -
  // phi) y la caja sigue derecha (cerca del jugador apenas esta torcida). 'ground': el suelo de
  // abajo del mundo, que no gira (es igual en toda la vuelta).
  struct Solid {
    BoundingBox box;
    float theta = 0.0f;
    bool ground = false;
  };

  // Lo que la ciudad genera para un trozo (sin GPU: se hace en otro hilo). Todo en los ejes de la
  // ciudad (sin girar).
  struct ChunkData {
    ObjData solid;
    ObjData glow;
    std::vector<Solid> solids;
    std::vector<PointLight> lights; // en coordenadas del trozo
    struct Pod {
      Vector3 position;
      float roll;
      float speed;
    };
    std::vector<Pod> pods;
    std::vector<Emitter> emitters; // la musica de cada tienda (coordenadas del trozo)
    struct Door {
      Vector3 center;
      Vector3 half;
    };
    std::vector<Door> doors; // donde suena la campanita
    std::vector<BoundingBox> interiors; // por dentro de cada tienda (alli solo suena su musica)
    // Un modelo ya hecho puesto en el trozo (la tienda, sus placas), con lo que se lee al apuntarle.
    struct Placed {
      std::string model;
      Vector3 position;
      float yaw = 0.0f;
      bool emissive = false;
      BoxCollider box; // en los ejes del modelo
      std::string prompt;
      std::string message;
      float roll = 0.0f;           // girado en Z (una puerta en una torre que no esta abajo)
      std::vector<Action> actions; // lo que hace con E (si no, 'message')
    };
    std::vector<Placed> placed;
  };

private:
  // Cada entidad del trozo, como esta en la ciudad (sin girar): Turn la pone en el mundo.
  struct Piece {
    std::string name;
    Vector2 xy{0.0f, 0.0f}; // (x, y) en la ciudad
    float roll = 0.0f;      // su giro en Z en la ciudad (grados)
  };
  // Todo en los ejes de la ciudad, con z ya en el mundo de ahora.
  struct Chunk {
    int lod = -1;
    long long anchor = 0; // el paso de detalle con el que se hizo (ver CylinderCity::anchor_)
    std::vector<Piece> pieces;
    std::vector<PointLight> lights;
    std::vector<Emitter> emitters;
    std::vector<Solid> solids;
    std::vector<std::pair<std::string, ChunkData::Door>> doors; // triggers de la campanita
    std::vector<BoundingBox> interiors;
  };

  void Place(Scene& scene, long long index, int lod, long long anchor, ChunkData data);
  void Drop(Scene& scene, Chunk& chunk);
  // Lleva todo lo de los trozos al mundo con el 'phi' de ahora: entidades, luces, sonidos,
  // colisiones y triggers.
  void Turn(Scene& scene);
  float AnchorDegrees(long long anchor) const;
  void ApplyCycle(Scene& scene, float dt);
  std::shared_ptr<ModelResource> Upload(const ObjData& data);
  std::shared_ptr<ModelResource> Model(const std::string& path); // un .obj ya hecho (la tienda)

  CityConfig cfg_;
  RetroEngine& engine_;
  long long origin_ = 0; // el trozo que esta en z = 0
  double phi_ = 0.0;     // lo que ha girado la ciudad (grados): el jugador esta en theta = phi
  long long anchor_ = 0; // el paso de detalle de ahora: el detalle se hace alrededor de el
  std::map<long long, Chunk> chunks_;
  std::map<std::tuple<long long, int, long long>, std::future<ChunkData>> pending_;
  std::map<std::string, std::shared_ptr<TextureResource>> textures_;
  std::shared_ptr<ModelResource> podModel_;
  std::map<std::string, std::shared_ptr<ModelResource>> models_; // los modelos ya hechos (la tienda)
  float time_ = 0.0f;
  bool indoors_ = false;
  bool first_ = true;
};

} // namespace retro
