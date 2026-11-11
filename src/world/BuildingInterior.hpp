#pragma once

#include "graphics/Light.hpp"
#include "world/BuildingVisit.hpp"
#include "world/MeshBuilder.hpp"
#include "world/MeshUpload.hpp"

#include <raylib.h>

#include <string>
#include <vector>

namespace retro {

class RetroEngine;
class Scene;

// Por dentro de una torre de la ciudad del cilindro (la escena la pide con "building": { ... }). Se
// genera en codigo: una planta de 36 x 24 m con el nucleo al centro (la escalera de ida y vuelta y
// dos elevadores que no sirven) y alrededor lo de su tipo y su variante. La escalera no se acaba:
// al llegar al descanso del piso de arriba, todo baja un piso sin que se note y el numero sube;
// asi se sube piso a piso los 'floors' de la torre. La planta baja tiene el vestibulo y la salida;
// encima del ultimo piso, la puerta de la azotea (que da a la ciudad, arriba de esa misma torre).
struct BuildingConfig {
  enum class Kind { Offices, Apartments, Hotel };
  Kind kind = Kind::Offices;
  // La variante: oficinas 0 abiertas / 1 privados alrededor del nucleo / 2 pasillo con privados;
  // departamentos 0 anillo / 1 pasillo / 2 anillo con departamentos grandes; hotel 0 pasillo /
  // 1 anillo / 2 anillo con suites.
  int layout = 0;
  float floorHeight = 3.6f;
  struct Textures {
    std::string floor, corridor, lobby, wall, corridorWall, ceiling, light, concrete, metal, wood, daylight,
        fabric, fabric2, bed, screen, counter, plant, mail, kitchen, partition, rug;
  } textures;
  gen::GlyphGrid glyphs;    // los numeros de piso y de puerta
  std::string stairDoor;    // modelo: puerta de lamina (1 x 2.1 m, el frente mira a +Z, origen abajo al centro)
  std::string exitDoor;     // modelo: puerta doble de vidrio (2.4 x 2.7 m, igual)
  std::string openPrompt = "Abrir";
  std::string exitPrompt = "Salir";
  std::string roofPrompt = "Salir a la azotea";
};

class BuildingInterior {
public:
  // Arma el piso de 'visit.floor' y pone el inicio de la escena: dentro de la puerta del vestibulo, o
  // (si se baja de la azotea) en el descanso de arriba. Si luego se pide otra posicion, gana esa.
  BuildingInterior(BuildingConfig config, BuildingVisit visit, RetroEngine& engine, Scene& scene);
  ~BuildingInterior();

  BuildingInterior(const BuildingInterior&) = delete;
  BuildingInterior& operator=(const BuildingInterior&) = delete;

  // Si 'feet' llego por la escalera al piso de arriba (o al de abajo), cambia de piso y rehace todo un
  // piso mas abajo (o arriba): devuelve cuanto hay que mover al jugador en Y (0 si nada).
  float Update(Scene& scene, Vector3 feet);

  int Floor() const noexcept { return visit_.floor; }
  int Top() const noexcept { return visit_.floors - 1; }

private:
  void Build(Scene& scene);

  BuildingConfig cfg_;
  BuildingVisit visit_;
  RetroEngine& engine_;
  TextureCache textures_;
  std::vector<std::string> entities_;
};

} // namespace retro
