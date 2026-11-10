#pragma once

#include <raylib.h>

#include <cstdint>
#include <string>

namespace retro {

// Una torre de la ciudad del cilindro en la que se entro: de donde se viene, a donde se vuelve y por
// donde se sale a la azotea. La ciudad la arma al poner la puerta (todo sale de la torre, nada del
// jugador), el SceneManager la guarda mientras se esta dentro y la partida tambien.
struct BuildingVisit {
  std::string interior;   // la escena de dentro (una por tipo y variante)
  std::string street;     // la escena de la ciudad
  int floors = 60;        // pisos con planta (0 = planta baja ... floors-1); encima, la azotea
  int floor = 0;          // en el que se esta
  std::uint64_t seed = 0; // lo que cambia de una torre a otra del mismo tipo

  // Donde esta la torre: su trozo (absoluto) y su angulo alrededor del cilindro. Para volver, la
  // ciudad empieza con ese trozo en z = 0 y girada 'theta' (la torre queda abajo).
  long long chunk = 0;
  double theta = 0.0;
  Vector3 door{0.0f, 0.0f, 0.0f}; // fuera de la puerta del vestibulo (en ese mundo)
  float doorYaw = 0.0f;
  Vector3 roof{0.0f, 0.0f, 0.0f}; // en la azotea, delante de la caseta de la escalera
  float roofYaw = 0.0f;
};

} // namespace retro
