#pragma once

// Aspecto del editor: fuente, iconos, paleta y unos pocos widgets con el estilo comun.
// Inspirado en Fluent (Windows 11): fondos grises en capas, esquinas redondeadas, un solo color
// de acento para lo seleccionado y lo activo.

#include <imgui.h>

// Iconos de Font Awesome 6 (vienen con rlImGui): ICON_FA_CUBE, ICON_FA_TRASH...
#include <extras/IconsFontAwesome6.h>

namespace editor::theme {

// Colores con nombre, para no repetir valores sueltos por el codigo.
inline const ImVec4 kAccent{0.30f, 0.76f, 1.00f, 1.00f};  // azul de acento
inline const ImVec4 kError{1.00f, 0.42f, 0.42f, 1.00f};   // rojo
inline const ImVec4 kWarning{1.00f, 0.78f, 0.30f, 1.00f}; // ambar
inline const ImVec4 kSuccess{0.45f, 0.85f, 0.50f, 1.00f}; // verde
inline const ImVec4 kTextDim{0.60f, 0.60f, 0.62f, 1.00f}; // texto secundario

// Color de fondo de la ventana (lo que se ve detras de los paneles).
inline constexpr unsigned char kClear[4] = {28, 28, 30, 255};

// Callback para rlImGui: carga Segoe UI (o la fuente por defecto si no existe) y le une los
// iconos. Se instala con rlImGuiSetLoadFontsCallback ANTES de rlImGuiSetup.
void LoadFonts();

// Espaciado, redondeos y paleta. Se llama despues de rlImGuiSetup.
void Apply();

// Boton de icono que se ve "encendido" cuando 'active' (barra de herramientas del visor).
bool ToolToggle(const char* icon, bool active, const char* tooltip);

// Boton de icono sin fondo, con descripcion al pasar el raton (Subir, Bajar, Quitar...).
bool IconButton(const char* icon, const char* tooltip);

// Separador vertical entre grupos de botones en una misma linea.
void ToolSeparator();

// Seccion plegable del inspector ("Transformacion", "Luz propia"...). Devuelve true si esta
// abierta: el contenido se dibuja solo entonces.
bool Section(const char* label, bool defaultOpen = true);

} // namespace editor::theme
