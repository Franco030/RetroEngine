#pragma once

#include "assets/Resources.hpp"

#include <raylib.h>

#include <memory>
#include <string>

namespace retro {

// Capa de interfaz dibujada a la resolución de la VENTANA, encima de la imagen.
// Las coordenadas y tamaños se dan en una cuadrícula de referencia de 320 de
// ancho, así el diseño no cambia si subes la resolución interna del motor.
class Ui {
public:
  static constexpr float kRefWidth = 320.0f;

  explicit Ui(std::shared_ptr<FontResource> font);

  void Begin(Rectangle area);

  float Width() const noexcept { return kRefWidth; }
  float Height() const noexcept { return refHeight_; }

  void Text(const std::string& text, float x, float y, float size, Color color,
            bool shadow = true) const;
  float TextWidth(const std::string& text, float size) const;

  void Rect(float x, float y, float w, float h, Color color) const;
  void RectLines(float x, float y, float w, float h, Color color, float thickness = 0.5f) const;

private:
  Rectangle ToWindow(float x, float y, float w, float h) const;

  std::shared_ptr<FontResource> fontRes_;
  Font font_{};
  bool isDefaultFont_ = true;

  Vector2 origin_{0.0f, 0.0f};
  float scale_ = 1.0f;
  float refHeight_ = 240.0f;
};

} // namespace retro