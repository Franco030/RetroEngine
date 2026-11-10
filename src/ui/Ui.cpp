#include "ui/Ui.hpp"

#include <algorithm>
#include <utility>

namespace retro {

Ui::Ui(std::shared_ptr<FontResource> font) : fontRes_(std::move(font)) {
  if (fontRes_ && fontRes_->IsValid()) {
    font_ = fontRes_->Get();
    isDefaultFont_ = false;
  } else {
    font_ = GetFontDefault();
    isDefaultFont_ = true;
  }
}

void Ui::Begin(Rectangle area) {
  origin_ = {area.x, area.y};
  scale_ = std::max(area.width / kRefWidth, 0.0001f);
  refHeight_ = area.height / scale_;
}

Rectangle Ui::ToWindow(float x, float y, float w, float h) const {
  return {origin_.x + x * scale_, origin_.y + y * scale_, w * scale_, h * scale_};
}

void Ui::Text(const std::string& text, float x, float y, float size, Color color,
              bool shadow) const {
  const float px = size * scale_;
  const float spacing = isDefaultFont_ ? px / 10.0f : 0.0f;
  const Vector2 pos{origin_.x + x * scale_, origin_.y + y * scale_};

  if (shadow) {
    const float off = std::max(1.0f, scale_ * 0.5f);
    DrawTextEx(font_, text.c_str(), {pos.x + off, pos.y + off}, px, spacing, {0, 0, 0, 200});
  }
  DrawTextEx(font_, text.c_str(), pos, px, spacing, color);
}

float Ui::TextWidth(const std::string& text, float size) const {
  const float px = size * scale_;
  const float spacing = isDefaultFont_ ? px / 10.0f : 0.0f;
  return MeasureTextEx(font_, text.c_str(), px, spacing).x / scale_;
}

void Ui::Rect(float x, float y, float w, float h, Color color) const {
  DrawRectangleRec(ToWindow(x, y, w, h), color);
}

void Ui::RectLines(float x, float y, float w, float h, Color color, float thickness) const {
  DrawRectangleLinesEx(ToWindow(x, y, w, h), std::max(1.0f, thickness * scale_), color);
}

} // namespace retro