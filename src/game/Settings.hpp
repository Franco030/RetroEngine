#pragma once

#include "graphics/PostProcess.hpp"

#include <filesystem>

namespace retro {

// Lo que el jugador elige en "Configuracion". Se guarda en un JSON pequeno junto a las partidas
// (ajustes.json) y se aplica al arrancar.
struct Settings {
  float volume = 1.0f;       // volumen general, 0..1
  int sensitivity = 5;       // raton, de 1 a 10 (5 = lo de siempre)
  bool fullscreen = false;   // ventana sin bordes a pantalla completa (F11)
  FilterMode filter = FilterMode::SharpBilinear;
  bool effects = true;       // grano, vineta y tinte del post-proceso

  // Grados por pixel de raton que corresponden a 'sensitivity'.
  float MouseDegreesPerPixel() const noexcept { return 0.04f + 0.02f * (sensitivity - 1); }
};

// Si el archivo no existe da los valores por defecto; si esta roto, avisa por consola y tambien.
Settings LoadSettings(const std::filesystem::path& file);
// Escritura atomica (como las partidas): un fallo a medias no deja el archivo roto.
void SaveSettings(const std::filesystem::path& file, const Settings& settings);

} // namespace retro
