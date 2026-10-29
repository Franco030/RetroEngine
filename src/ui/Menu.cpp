#include "ui/Menu.hpp"

#include <algorithm>

namespace retro {

namespace {

constexpr float kSize = 11.0f;
constexpr float kLineH = 16.0f;
constexpr Color kNormal{176, 168, 160, 255};
constexpr Color kSelected{236, 204, 128, 255};
constexpr Color kDisabled{92, 86, 84, 255};

Color Faded(Color c, float alpha) {
  c.a = static_cast<unsigned char>(static_cast<float>(c.a) * std::clamp(alpha, 0.0f, 1.0f));
  return c;
}

std::string Line(const Menu::Item& item) {
  return item.value.empty() ? item.label : item.label + ":  < " + item.value + " >";
}

} // namespace

void Menu::SetItems(std::vector<Item> items) {
  items_ = std::move(items);
  hits_.clear();
  if (items_.empty()) {
    selected_ = 0;
    return;
  }
  selected_ = std::min(selected_, items_.size() - 1);
  if (!items_[selected_].enabled)
    Step(1);
}

void Menu::Select(std::size_t index) {
  if (index < items_.size() && items_[index].enabled)
    selected_ = index;
}

void Menu::Step(int direction) {
  const auto n = static_cast<int>(items_.size());
  int i = static_cast<int>(selected_);
  for (int k = 0; k < n; ++k) { // salta las desactivadas
    i = (i + direction + n) % n;
    if (items_[static_cast<std::size_t>(i)].enabled) {
      selected_ = static_cast<std::size_t>(i);
      return;
    }
  }
}

Menu::Result Menu::Update(Vector2 mouse) {
  if (items_.empty())
    return {};
  const std::size_t before = selected_;
  const int id = items_[selected_].id;

  if (IsKeyPressed(KEY_ESCAPE))
    return {Event::Back, id};
  if (IsKeyPressed(KEY_W) || IsKeyPressed(KEY_UP))
    Step(-1);
  if (IsKeyPressed(KEY_S) || IsKeyPressed(KEY_DOWN))
    Step(1);

  const bool hasValue = !items_[selected_].value.empty();
  if (IsKeyPressed(KEY_E) || IsKeyPressed(KEY_ENTER) || IsKeyPressed(KEY_SPACE))
    return {hasValue ? Event::Right : Event::Accept, items_[selected_].id};
  if (hasValue && (IsKeyPressed(KEY_A) || IsKeyPressed(KEY_LEFT)))
    return {Event::Left, items_[selected_].id};
  if (hasValue && (IsKeyPressed(KEY_D) || IsKeyPressed(KEY_RIGHT)))
    return {Event::Right, items_[selected_].id};

  // Raton: solo elige si se ha movido (si no, pisaria lo elegido con el teclado).
  const bool moved = mouse.x != lastMouse_.x || mouse.y != lastMouse_.y;
  lastMouse_ = mouse;
  for (std::size_t i = 0; i < hits_.size() && i < items_.size(); ++i) {
    if (!items_[i].enabled || !CheckCollisionPointRec(mouse, hits_[i]))
      continue;
    if (moved)
      selected_ = i;
    if (IsMouseButtonPressed(MOUSE_BUTTON_LEFT)) {
      selected_ = i;
      if (items_[i].value.empty())
        return {Event::Accept, items_[i].id};
      const bool left = mouse.x < hits_[i].x + hits_[i].width * 0.5f;
      return {left ? Event::Left : Event::Right, items_[i].id};
    }
  }
  return {selected_ != before ? Event::Moved : Event::None, items_[selected_].id};
}

void Menu::Draw(const Ui& ui, float top, float alpha) const {
  hits_.assign(items_.size(), Rectangle{});
  for (std::size_t i = 0; i < items_.size(); ++i) {
    const Item& item = items_[i];
    const std::string text = Line(item);
    const float w = ui.TextWidth(text, kSize);
    const float x = (ui.Width() - w) * 0.5f;
    const float y = top + static_cast<float>(i) * kLineH;
    const bool sel = i == selected_ && item.enabled;
    const Color c = !item.enabled ? kDisabled : sel ? kSelected : kNormal;
    if (sel) { // un guion a cada lado de la elegida
      ui.Text("-", x - 12.0f, y, kSize, Faded(c, alpha));
      ui.Text("-", x + w + 6.0f, y, kSize, Faded(c, alpha));
    }
    ui.Text(text, x, y, kSize, Faded(c, alpha));
    hits_[i] = {x - 14.0f, y - 2.0f, w + 28.0f, kLineH};
  }
}

} // namespace retro
