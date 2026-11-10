#pragma once

#include "ui/Ui.hpp"

#include <raylib.h>

#include <cstddef>
#include <string>
#include <utility>
#include <vector>

namespace retro {

// Lista de opciones (pantalla de inicio, pausa, configuracion). Teclado: W/S o flechas para
// elegir, A/D o flechas para cambiar un valor, E/Enter/Espacio para aceptar y Esc para volver.
// Raton: pasar por encima elige, clic acepta (en una opcion con valor, la mitad izquierda baja y
// la derecha sube). Update decide y Draw dibuja; las zonas del raton son las del ultimo Draw.
class Menu {
public:
  struct Item {
    Item(int id, std::string label, std::string value = {}, bool enabled = true)
        : id(id), label(std::move(label)), value(std::move(value)), enabled(enabled) {}

    int id;
    std::string label;
    std::string value; // si no esta vacio se dibuja "< valor >" y se puede cambiar
    bool enabled;
  };

  // Como se ve: los colores de las opciones y, si 'mess' > 0, el texto como una senal que no
  // sintoniza (DrawGarbled; la elegida un poco menos, para que se lea).
  struct Style {
    Color normal{176, 168, 160, 255};
    Color selected{236, 204, 128, 255};
    Color disabled{92, 86, 84, 255};
    float mess = 0.0f;
  };
  void SetStyle(const Style& style) { style_ = style; }

  enum class Event { None, Moved, Accept, Left, Right, Back };
  struct Result {
    Event event = Event::None;
    int id = 0; // la opcion a la que se refiere (Accept, Left, Right)
  };

  // Cambia las opciones; la seleccion se queda en la misma posicion si sigue existiendo.
  void SetItems(std::vector<Item> items);
  void Select(std::size_t index);

  // 'mouse' en coordenadas de la Ui (rejilla de 320 de ancho).
  Result Update(Vector2 mouse);
  void Draw(const Ui& ui, float top, float alpha = 1.0f) const;
  // Lo que ocupa de alto la lista (para colocarla).
  float Height() const noexcept;

private:
  void Step(int direction);

  std::vector<Item> items_;
  Style style_;
  std::size_t selected_ = 0;
  Vector2 lastMouse_{-1.0f, -1.0f};
  mutable std::vector<Rectangle> hits_; // zona de cada opcion en el ultimo Draw
};

} // namespace retro
