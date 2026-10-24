#include "EditorTheme.hpp"

#include <imgui_internal.h>
#include <raylib.h>

// Los datos comprimidos de la fuente de iconos. El archivo define arreglos 'static', asi que se
// incluye en UN solo .cpp (este).
#include <extras/FA6FreeSolidFontData.h>

#include <string>

namespace editor::theme {

namespace {

constexpr float kFontSize = 16.0f;

ImVec4 Rgb(int r, int g, int b, float a = 1.0f) {
  return ImVec4(static_cast<float>(r) / 255.0f, static_cast<float>(g) / 255.0f,
                static_cast<float>(b) / 255.0f, a);
}

ImVec4 WithAlpha(ImVec4 c, float a) {
  c.w = a;
  return c;
}

} // namespace

void LoadFonts() {
  ImGuiIO& io = ImGui::GetIO();

  // Segoe UI es la fuente de Windows; en otro sistema se usa la de ImGui.
  ImFont* font = nullptr;
  const char* segoe = "C:/Windows/Fonts/segoeui.ttf";
  if (FileExists(segoe))
    font = io.Fonts->AddFontFromFileTTF(segoe, kFontSize);
  if (!font) {
    ImFontConfig fallback;
    fallback.SizePixels = 13.0f;
    io.Fonts->AddFontDefault(&fallback);
  }

  // Los iconos se UNEN a la fuente anterior (MergeMode): un texto puede mezclar letras e iconos
  // ("\xef\x86\xb2 casa"). GlyphMinAdvanceX da a todos el mismo ancho, asi quedan alineados.
  static const ImWchar ranges[] = {ICON_MIN_FA, ICON_MAX_FA, 0};
  ImFontConfig icons;
  icons.MergeMode = true;
  icons.PixelSnapH = true;
  icons.FontDataOwnedByAtlas = false; // los datos son de FA6FreeSolidFontData.h, no del atlas
  icons.GlyphMinAdvanceX = kFontSize;
  icons.GlyphOffset = ImVec2(0.0f, 1.0f);
  io.Fonts->AddFontFromMemoryCompressedTTF(fa_solid_900_compressed_data,
                                           static_cast<int>(fa_solid_900_compressed_size),
                                           kFontSize * 0.85f, &icons, ranges);
}

void Apply() {
  ImGuiStyle& s = ImGui::GetStyle();

  // --- espaciado y formas ---
  s.WindowPadding = ImVec2(10.0f, 10.0f);
  s.FramePadding = ImVec2(8.0f, 5.0f);
  s.CellPadding = ImVec2(6.0f, 4.0f);
  s.ItemSpacing = ImVec2(8.0f, 6.0f);
  s.ItemInnerSpacing = ImVec2(6.0f, 4.0f);
  s.IndentSpacing = 18.0f;
  s.ScrollbarSize = 12.0f;
  s.GrabMinSize = 10.0f;

  s.WindowBorderSize = 1.0f;
  s.ChildBorderSize = 1.0f;
  s.PopupBorderSize = 1.0f;
  s.FrameBorderSize = 0.0f;
  s.TabBorderSize = 0.0f;

  s.WindowRounding = 6.0f;
  s.ChildRounding = 6.0f;
  s.FrameRounding = 5.0f;
  s.PopupRounding = 6.0f;
  s.ScrollbarRounding = 8.0f;
  s.GrabRounding = 4.0f;
  s.TabRounding = 5.0f;

  s.WindowTitleAlign = ImVec2(0.0f, 0.5f);
  s.DisabledAlpha = 0.35f; // lo desactivado se distingue bien
  s.WindowMenuButtonPosition = ImGuiDir_None; // sin la flechita de plegar en cada panel
  s.SeparatorTextBorderSize = 1.0f;
  s.SeparatorTextPadding = ImVec2(4.0f, 6.0f);

  // --- paleta: capas de gris (fondo < panel < campo) y un acento ---
  const ImVec4 bg = Rgb(28, 28, 30);
  const ImVec4 panel = Rgb(36, 36, 39);
  const ImVec4 field = Rgb(48, 48, 52);
  const ImVec4 fieldHover = Rgb(58, 58, 63);
  const ImVec4 fieldActive = Rgb(66, 66, 72);
  const ImVec4 line = ImVec4(1.0f, 1.0f, 1.0f, 0.08f);

  ImVec4* c = s.Colors;
  c[ImGuiCol_Text] = Rgb(236, 236, 240);
  c[ImGuiCol_TextDisabled] = kTextDim;
  c[ImGuiCol_WindowBg] = panel;
  c[ImGuiCol_ChildBg] = ImVec4(0.0f, 0.0f, 0.0f, 0.0f);
  c[ImGuiCol_PopupBg] = WithAlpha(Rgb(44, 44, 48), 0.98f);
  c[ImGuiCol_Border] = line;
  c[ImGuiCol_BorderShadow] = ImVec4(0.0f, 0.0f, 0.0f, 0.0f);

  c[ImGuiCol_FrameBg] = field;
  c[ImGuiCol_FrameBgHovered] = fieldHover;
  c[ImGuiCol_FrameBgActive] = fieldActive;

  c[ImGuiCol_TitleBg] = bg;
  c[ImGuiCol_TitleBgActive] = bg;
  c[ImGuiCol_TitleBgCollapsed] = bg;
  c[ImGuiCol_MenuBarBg] = bg;

  c[ImGuiCol_ScrollbarBg] = ImVec4(0.0f, 0.0f, 0.0f, 0.0f);
  c[ImGuiCol_ScrollbarGrab] = Rgb(74, 74, 80);
  c[ImGuiCol_ScrollbarGrabHovered] = Rgb(94, 94, 100);
  c[ImGuiCol_ScrollbarGrabActive] = Rgb(114, 114, 120);

  c[ImGuiCol_CheckMark] = kAccent;
  c[ImGuiCol_SliderGrab] = kAccent;
  c[ImGuiCol_SliderGrabActive] = Rgb(120, 205, 255);

  c[ImGuiCol_Button] = field;
  c[ImGuiCol_ButtonHovered] = fieldHover;
  c[ImGuiCol_ButtonActive] = fieldActive;

  // Header = fondo de lo seleccionado (jerarquia, desplegables) y de las cabeceras plegables.
  c[ImGuiCol_Header] = WithAlpha(kAccent, 0.38f);
  c[ImGuiCol_HeaderHovered] = ImVec4(1.0f, 1.0f, 1.0f, 0.07f);
  c[ImGuiCol_HeaderActive] = WithAlpha(kAccent, 0.34f);

  c[ImGuiCol_Separator] = line;
  c[ImGuiCol_SeparatorHovered] = WithAlpha(kAccent, 0.60f);
  c[ImGuiCol_SeparatorActive] = kAccent;

  c[ImGuiCol_ResizeGrip] = ImVec4(0.0f, 0.0f, 0.0f, 0.0f);
  c[ImGuiCol_ResizeGripHovered] = WithAlpha(kAccent, 0.40f);
  c[ImGuiCol_ResizeGripActive] = WithAlpha(kAccent, 0.70f);

  c[ImGuiCol_Tab] = bg;
  c[ImGuiCol_TabHovered] = fieldHover;
  c[ImGuiCol_TabSelected] = panel;
  c[ImGuiCol_TabSelectedOverline] = kAccent;
  c[ImGuiCol_TabDimmed] = bg;
  c[ImGuiCol_TabDimmedSelected] = panel;
  c[ImGuiCol_TabDimmedSelectedOverline] = ImVec4(0.0f, 0.0f, 0.0f, 0.0f);

  c[ImGuiCol_DockingPreview] = WithAlpha(kAccent, 0.45f);
  c[ImGuiCol_DockingEmptyBg] = bg;

  c[ImGuiCol_TableHeaderBg] = field;
  c[ImGuiCol_TableBorderStrong] = line;
  c[ImGuiCol_TableBorderLight] = line;
  c[ImGuiCol_TableRowBgAlt] = ImVec4(1.0f, 1.0f, 1.0f, 0.03f);

  c[ImGuiCol_TextLink] = kAccent;
  c[ImGuiCol_TextSelectedBg] = WithAlpha(kAccent, 0.35f);
  c[ImGuiCol_InputTextCursor] = c[ImGuiCol_Text];
  c[ImGuiCol_TreeLines] = ImVec4(1.0f, 1.0f, 1.0f, 0.12f);
  c[ImGuiCol_DragDropTarget] = kAccent;
  c[ImGuiCol_NavCursor] = kAccent;
  c[ImGuiCol_ModalWindowDimBg] = ImVec4(0.0f, 0.0f, 0.0f, 0.55f);
}

bool ToolToggle(const char* icon, bool active, const char* tooltip) {
  if (active) {
    ImGui::PushStyleColor(ImGuiCol_Button, WithAlpha(kAccent, 0.30f));
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, WithAlpha(kAccent, 0.40f));
    ImGui::PushStyleColor(ImGuiCol_ButtonActive, WithAlpha(kAccent, 0.50f));
  }
  // "##" + descripcion: dos botones con el mismo icono no comparten ID.
  const std::string label = std::string(icon) + "##" + tooltip;
  const float size = ImGui::GetFrameHeight();
  const bool clicked = ImGui::Button(label.c_str(), ImVec2(size * 1.25f, size));
  if (active)
    ImGui::PopStyleColor(3);
  if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayShort | ImGuiHoveredFlags_AllowWhenDisabled))
    ImGui::SetTooltip("%s", tooltip);
  return clicked;
}

bool IconButton(const char* icon, const char* tooltip) {
  ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.0f, 0.0f, 0.0f, 0.0f));
  ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(4.0f, 2.0f));
  const std::string label = std::string(icon) + "##" + tooltip;
  const bool clicked = ImGui::Button(label.c_str());
  ImGui::PopStyleVar();
  ImGui::PopStyleColor();
  if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayShort | ImGuiHoveredFlags_AllowWhenDisabled))
    ImGui::SetTooltip("%s", tooltip);
  return clicked;
}

void ToolSeparator() {
  ImGui::SameLine();
  ImGui::SeparatorEx(ImGuiSeparatorFlags_Vertical);
  ImGui::SameLine();
}

bool Section(const char* label, bool defaultOpen) {
  ImGui::Spacing();
  // Gris neutro: el acento se reserva para lo seleccionado.
  ImGui::PushStyleColor(ImGuiCol_Header, ImVec4(1.0f, 1.0f, 1.0f, 0.05f));
  ImGui::PushStyleColor(ImGuiCol_HeaderHovered, ImVec4(1.0f, 1.0f, 1.0f, 0.09f));
  ImGui::PushStyleColor(ImGuiCol_HeaderActive, ImVec4(1.0f, 1.0f, 1.0f, 0.12f));
  const ImGuiTreeNodeFlags flags = defaultOpen ? ImGuiTreeNodeFlags_DefaultOpen : 0;
  const bool open = ImGui::CollapsingHeader(label, flags);
  ImGui::PopStyleColor(3);
  return open;
}

} // namespace editor::theme
