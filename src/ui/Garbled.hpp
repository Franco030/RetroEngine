#pragma once

#include "ui/Ui.hpp"

#include <raylib.h>

#include <cstdint>
#include <string>

namespace retro {

// El texto como una senal que no sintoniza: cada letra cambia por otra o se desplaza a ratos, con
// los bordes rojo y cian corridos, franjas oscuras que lo rasgan y, de vez en cuando, un instante
// casi limpio. 'mess' (0..1): cuanto se estropea (0.2 se lee; 0.85 casi no). Centrado en 'cx'.
// 'seed' separa dos textos que se dibujan a la vez (que no fallen igual).
void DrawGarbled(const Ui& ui, const std::string& text, float cx, float y, float size, Color color,
                 double time, std::uint32_t seed, float mess);

} // namespace retro
