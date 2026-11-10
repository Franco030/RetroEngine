#include "ui/Garbled.hpp"

#include <cstddef>

namespace retro {

namespace {

// Un numero "al azar" que solo depende de sus entradas: el mismo en todo el trozo de tiempo.
std::uint32_t Noise(std::uint32_t a, std::uint32_t b) {
  std::uint32_t h = a * 0x9E3779B1u ^ (b + 0x7F4A7C15u) * 0x85EBCA77u;
  h ^= h >> 15;
  h *= 0x2C1B3C6Du;
  h ^= h >> 12;
  return h;
}

} // namespace

void DrawGarbled(const Ui& ui, const std::string& text, float cx, float y, float size, Color color,
                 double time, std::uint32_t seed, float mess) {
  static constexpr char kJunk[] = "#%&@$?/\\|<>=+*~^XVMNWAETSU0123456789";
  const auto slice = static_cast<std::uint32_t>(time * 12.0); // 12 cambios por segundo
  // un instante casi limpio de vez en cuando (unas 3 de cada 40 franjas de 1/12 s)
  const bool clear = Noise(slice / 2u, seed) % 40u < 3u;
  const float m = clear ? mess * 0.2f : mess;
  const float width = ui.TextWidth(text, size);
  float start = cx - width * 0.5f;
  // toda la linea salta de lado a ratos
  if (Noise(slice, seed + 7u) % 100u < static_cast<std::uint32_t>(30.0f * m))
    start += static_cast<float>(static_cast<int>(Noise(slice, seed + 8u) % 7u) - 3);
  for (std::size_t i = 0; i < text.size(); ++i) {
    const std::string original(1, text[i]);
    // donde iria la letra en el texto entero (asi cuenta el espaciado de la fuente)
    const float x = start + ui.TextWidth(text.substr(0, i + 1), size) - ui.TextWidth(original, size);
    const std::uint32_t r = Noise(slice ^ (static_cast<std::uint32_t>(i) * 131u), seed);
    std::string glyph = original;
    if (text[i] != ' ' && r % 100u < static_cast<std::uint32_t>(70.0f * m))
      glyph.assign(1, kJunk[(r >> 8) % (sizeof kJunk - 1)]);
    const float dx = static_cast<float>(static_cast<int>((r >> 16) % 3u) - 1) * m;
    const float dy = (r >> 20) % 100u < static_cast<std::uint32_t>(25.0f * m)
                         ? static_cast<float>(static_cast<int>((r >> 24) % 5u) - 2)
                         : 0.0f;
    if (glyph != " ") {
      // los bordes de color, corridos a los lados
      const auto alpha = static_cast<unsigned char>(color.a * 0.5f);
      ui.Text(glyph, x + dx - 1.5f * m - 0.5f, y + dy, size, {255, 40, 60, alpha}, false);
      ui.Text(glyph, x + dx + 1.5f * m + 0.5f, y + dy, size, {40, 220, 255, alpha}, false);
      // la letra, que parpadea
      Color c = color;
      c.a = static_cast<unsigned char>(color.a * (0.45f + 0.55f * static_cast<float>((r >> 4) % 100u) / 100.0f));
      ui.Text(glyph, x + dx, y + dy, size, c);
    }
  }
  // franjas oscuras que cortan el texto (rasgado)
  for (std::uint32_t k = 0; k < 3; ++k) {
    const std::uint32_t r = Noise(slice + k * 977u, seed + 3u);
    if (r % 100u >= static_cast<std::uint32_t>(70.0f * m))
      continue;
    const float band = y + static_cast<float>(r % static_cast<std::uint32_t>(size + 2.0f)) - 1.0f;
    ui.Rect(cx - width * 0.6f, band, width * 1.2f, 1.0f + static_cast<float>((r >> 9) % 2u),
            {8, 6, 14, static_cast<unsigned char>(color.a * 0.85f)});
  }
}

} // namespace retro
