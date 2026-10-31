#include <raylib.h>
#include <raymath.h>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <ctime>
#include <exception>
#include <filesystem>
#include <iostream>
#include <optional>
#include <string>
#include <variant>
#include <vector>

#include "audio/AudioSystem.hpp"
#include "audio/Footsteps.hpp"
#include "core/RetroEngine.hpp"
#include "game/GameState.hpp"
#include "game/SaveGame.hpp"
#include "game/Settings.hpp"
#include "graphics/DebugDraw.hpp"
#include "graphics/RetroCamera.hpp"
#include "graphics/ShaderScope.hpp"
#include "physics/Collision.hpp"
#include "player/Player.hpp"
#include "ui/Menu.hpp"
#include "ui/Ui.hpp"
#include "world/ActionRunner.hpp"
#include "world/Dialogue.hpp"
#include "world/SceneManager.hpp"

static void HandleDebugKeys(retro::RetroEngine& engine) {
  static bool jitter = true, lowColor = true, dither = true;

  retro::RetroShaderParams p = engine.Retro().Params();
  bool changed = false;

  if (IsKeyPressed(KEY_ONE)) {
    jitter = !jitter;
    p.snapResolution = jitter ? Vector2{static_cast<float>(engine.Config().internalWidth),
                                        static_cast<float>(engine.Config().internalHeight)}
                              : Vector2{4096.0f, 4096.0f};
    changed = true;
  }
  if (IsKeyPressed(KEY_TWO)) {
    lowColor = !lowColor;
    p.colorLevels = lowColor ? 32.0f : 256.0f;
    changed = true;
  }
  if (IsKeyPressed(KEY_THREE)) {
    dither = !dither;
    p.ditherStrength = dither ? 1.0f : 0.0f;
    changed = true;
  }
  if (IsKeyPressed(KEY_FOUR)) {
    p.fogEnabled = !p.fogEnabled;
    changed = true;
  }
  if (IsKeyPressed(KEY_SIX)) {
    p.pointLights = !p.pointLights;
    changed = true;
  }
  if (changed)
    engine.Retro().SetParams(p);

  retro::PostParams q = engine.Post().Params();
  bool postChanged = false;

  if (IsKeyPressed(KEY_F7)) {
    q.filter = static_cast<retro::FilterMode>((static_cast<int>(q.filter) + 1) % 3);
    postChanged = true;
  }
  if (IsKeyPressed(KEY_F8)) {
    q.effects = !q.effects;
    postChanged = true;
  }
  if (IsKeyPressed(KEY_M)) {
    engine.Audio().SetMasterVolume(engine.Audio().MasterVolume() > 0.0f ? 0.0f : 1.0f);
  }
  if (postChanged)
    engine.Post().SetParams(q);
}

// Una linea de texto ya partida. 'start' es su posicion dentro del texto original.
struct TextLine {
  std::string text;
  std::size_t start;
};

// Parte 'text' en lineas de 'maxWidth' como mucho: respeta los saltos que trae y corta en los
// espacios. Una palabra que no cabe sola se queda entera.
static std::vector<TextLine> WrapText(const retro::Ui& ui, const std::string& text, float size,
                                      float maxWidth) {
  std::vector<TextLine> lines;
  std::size_t begin = 0;
  while (true) {
    const std::size_t end = std::min(text.find('\n', begin), text.size());
    while (ui.TextWidth(text.substr(begin, end - begin), size) > maxWidth) {
      std::size_t cut = std::string::npos;
      for (std::size_t sp = text.find(' ', begin); sp < end; sp = text.find(' ', sp + 1)) {
        if (ui.TextWidth(text.substr(begin, sp - begin), size) > maxWidth)
          break;
        cut = sp;
      }
      if (cut == std::string::npos)
        break;
      lines.push_back({text.substr(begin, cut - begin), begin});
      begin = cut + 1;
    }
    lines.push_back({text.substr(begin, end - begin), begin});
    if (end == text.size())
      return lines;
    begin = end + 1;
  }
}

// Frase del negro entre escenas (goto_scene "lines"): centrada en la pantalla, en un blanco
// calido que aparece y se apaga con 'alpha'.
static void DrawCard(const retro::Ui& ui, std::string_view text, float alpha) {
  constexpr float kSize = 11.0f, kLineH = 14.0f, kMargin = 14.0f;
  const auto a = static_cast<unsigned char>(alpha * 255.0f);
  const std::vector<TextLine> lines = WrapText(ui, std::string(text), kSize, ui.Width() - 2.0f * kMargin);
  float y = (ui.Height() - static_cast<float>(lines.size()) * kLineH) * 0.5f;
  for (const TextLine& line : lines) {
    ui.Text(line.text, (ui.Width() - ui.TextWidth(line.text, kSize)) * 0.5f, y, kSize,
            Color{226, 214, 196, a}, false);
    y += kLineH;
  }
}

// Subtitulo sobre la imagen, sin cuadro: blanco con contorno negro, centrado abajo. Solo se
// dibujan los 'visible' primeros bytes (el texto se escribe letra a letra), pero cada linea se
// coloca segun su ancho completo, asi no se mueve mientras crece. Encima va quien habla (si hay)
// y, con 'more', un triangulo que parpadea al final: E pasa de pagina.
static void DrawSubtitle(const retro::Ui& ui, const std::string& text, std::size_t visible,
                         const std::string& speaker, bool more) {
  constexpr float kSize = 11.0f, kLineH = 13.0f, kBorde = 0.75f, kMargin = 12.0f;
  const std::vector<TextLine> lines = WrapText(ui, text, kSize, ui.Width() - 2.0f * kMargin);

  auto outlined = [&](const std::string& s, float x, float y, float size, Color color) {
    for (int dx = -1; dx <= 1; ++dx) { // contorno: el mismo texto en negro alrededor
      for (int dy = -1; dy <= 1; ++dy) {
        if (dx != 0 || dy != 0)
          ui.Text(s, x + dx * kBorde, y + dy * kBorde, size, BLACK, false);
      }
    }
    ui.Text(s, x, y, size, color, false);
  };

  const float y0 = ui.Height() - 20.0f - static_cast<float>(lines.size()) * kLineH;
  if (!speaker.empty()) {
    constexpr float kSpeakerSize = 9.0f;
    const float x = (ui.Width() - ui.TextWidth(speaker, kSpeakerSize)) * 0.5f;
    outlined(speaker, x, y0 - 11.0f, kSpeakerSize, Color{214, 178, 110, 255});
  }
  float endX = 0.0f, endY = 0.0f;
  for (std::size_t i = 0; i < lines.size(); ++i) {
    if (visible <= lines[i].start)
      break;
    const std::string shown = lines[i].text.substr(0, visible - lines[i].start);
    const float x = (ui.Width() - ui.TextWidth(lines[i].text, kSize)) * 0.5f;
    const float y = y0 + static_cast<float>(i) * kLineH;
    outlined(shown, x, y, kSize, RAYWHITE);
    endX = x + ui.TextWidth(lines[i].text, kSize);
    endY = y;
  }

  if (more && visible >= text.size() && std::fmod(GetTime(), 0.8) < 0.5) {
    // Triangulo hacia abajo (filas de 7, 5, 3 y 1 pixeles): primero todo el contorno negro y
    // luego el blanco, si no el contorno de cada fila tapa la anterior.
    const float x = endX + 4.0f, y = endY + 3.0f;
    for (int r = 0; r < 4; ++r)
      ui.Rect(x - 1.0f + r, y - 1.0f + r, 9.0f - 2.0f * r, 3.0f, BLACK);
    for (int r = 0; r < 4; ++r)
      ui.Rect(x + r, y + r, 7.0f - 2.0f * r, 1.0f, RAYWHITE);
  }
}

// Sonido de las letras: un chasquido de estatica cada dos letras (no en espacios), alternando
// tres variantes para que no suene a metralleta.
class LetterSounds {
public:
  void Update(const retro::GameState& state, retro::AudioSystem& audio) {
    if (!state.HasMessage())
      return;
    if (state.MessageId() != id_) {
      id_ = state.MessageId();
      shown_ = 0;
    }
    const std::string& text = state.MessageText();
    const std::size_t now = state.VisibleBytes();
    for (; shown_ < now; ++shown_) {
      const unsigned char c = static_cast<unsigned char>(text[shown_]);
      if (c == ' ' || c == '\n' || (c & 0xC0) == 0x80)
        continue;
      if (++letters_ % 2 == 0) {
        static const char* const kFiles[] = {"audio/letra_1.wav", "audio/letra_2.wav",
                                             "audio/letra_3.wav"};
        audio.PlaySfx(kFiles[(letters_ / 2 + shown_) % 3], 0.32f);
      }
    }
  }

private:
  unsigned id_ = 0;
  std::size_t shown_ = 0;
  unsigned letters_ = 0;
};

// A donde se mira mientras se lee: el punto, lo rapido que se gira (look_at/scare "speed") y la
// vista estrecha (solo scare: "fov"; 0 = sin acercamiento). Van juntos para que al soltar el foco
// no quede el acercamiento de un susto pegado a la siguiente conversacion.
struct Focus {
  Vector3 point;
  float rate = 6.0f;
  float fov = 0.0f;
  float age = 0.0f;     // segundos girando hacia el
  bool settled = false; // la camara ya ha llegado: hasta entonces el jugador no se suelta
};

// Punto al que mirar durante un dialogo: el de la caja de interaccion de la entidad (o, si no
// tiene, la del modelo): la cara si es alta como una persona; si no, su centro.
static Vector3 DialogueFocus(retro::Entity& e) {
  retro::BoxCollider box;
  if (e.interact) {
    box = e.interact->box;
  } else {
    const BoundingBox local = e.LocalBounds();
    box.min = local.min;
    box.max = local.max;
  }
  const retro::WorldBox b = retro::MakeWorldBox(box, e.transform);
  const float alto = b.top - b.bottom;
  const float y = alto > 1.4f ? b.top - 0.4f : (b.top + b.bottom) * 0.5f;
  return {b.center.x, y, b.center.z};
}

// Si las acciones dicen algo (un mensaje o un dialogo): entonces la camara mira a quien habla.
static bool Speaks(const std::vector<retro::Action>& actions) {
  return std::any_of(actions.begin(), actions.end(), [](const retro::Action& a) {
    return std::holds_alternative<retro::action::Message>(a) ||
           std::holds_alternative<retro::action::StartDialogue>(a);
  });
}

// La pantalla de inicio es la feria: la camara da vueltas alrededor del "target" de la camara de
// la escena y suena el vals con percusion (en el juego, la feria suena sin ella).
constexpr const char* kGameTitle = "FESTUM VENTI"; // "la fiesta del viento", en latin
constexpr const char* kTitleScene = "scenes/feria.json";
constexpr const char* kTitleMusic = "audio/musica_feria_menu_v2.wav";
constexpr const char* kFirstScene = "scenes/demo.json"; // donde empieza una partida nueva
// La pantalla de inicio existe desde que la partida llega a la feria (la pone el anciano al
// empujarte dentro del arbol). Antes, el juego arranca jugando, como siempre.
constexpr const char* kTitleFlag = "bosque_sueno";

enum class Screen { Title, Playing, Paused };
enum class Page { Main, Settings };
enum Option : int {
  kPlay, kContinue, kNewGame, kSettings, kQuit, kResume, kToTitle,
  kVolume, kSensitivity, kFullscreen, kFilter, kEffects, kBack
};

static void ApplySettings(const retro::Settings& s, retro::RetroEngine& engine,
                          retro::Player& player) {
  engine.Audio().SetMasterVolume(s.volume);
  player.SetMouseSensitivity(s.MouseDegreesPerPixel());
  if (IsWindowState(FLAG_BORDERLESS_WINDOWED_MODE) != s.fullscreen)
    ToggleBorderlessWindowed();
  retro::PostParams q = engine.Post().Params();
  q.filter = s.filter;
  q.effects = s.effects;
  engine.Post().SetParams(q);
}

// Si la partida guardada mas reciente ya llego a la feria (tiene kTitleFlag).
static bool LatestSaveReachedTitle(const retro::SaveManager& saves, std::optional<int> slot) {
  if (!slot)
    return false;
  try {
    const retro::SaveData d = saves.Read(*slot);
    const auto it = d.flags.find(kTitleFlag);
    return it != d.flags.end() && it->second != 0;
  } catch (const std::exception& e) {
    std::cerr << "Carga: " << e.what() << '\n';
    return false;
  }
}

// Donde van las fotos (F12): Imagenes/Festum Venti del usuario; si no hay carpeta de imagenes,
// junto a las partidas.
static std::filesystem::path PhotosDirectory(const retro::SaveManager& saves) {
  for (const char* var : {"USERPROFILE", "HOME"}) {
    if (const char* home = std::getenv(var); home && *home) {
      const std::filesystem::path pics = std::filesystem::path(home) / "Pictures";
      if (std::filesystem::is_directory(pics))
        return pics / "Festum Venti";
    }
  }
  return saves.Directory().parent_path() / "fotos";
}

// festum_venti_AAAA-MM-DD_hh-mm-ss.png en 'dir' (con _2, _3... si ya existe).
static std::filesystem::path NewPhotoPath(const std::filesystem::path& dir) {
  const std::time_t t = std::time(nullptr);
  std::tm tm{};
#if defined(_MSC_VER)
  localtime_s(&tm, &t);
#elif defined(_WIN32)
  if (const std::tm* p = std::localtime(&t))
    tm = *p;
#else
  localtime_r(&t, &tm);
#endif
  char stamp[32];
  std::strftime(stamp, sizeof stamp, "%Y-%m-%d_%H-%M-%S", &tm);
  std::filesystem::path path = dir / (std::string("festum_venti_") + stamp + ".png");
  for (int n = 2; std::filesystem::exists(path); ++n)
    path = dir / (std::string("festum_venti_") + stamp + "_" + std::to_string(n) + ".png");
  return path;
}

// La partida guardada mas reciente (la ranura 1 o el autoguardado), si hay alguna.
static std::optional<int> LatestSave(const retro::SaveManager& saves) {
  std::optional<int> best;
  std::string bestAt;
  for (const int slot : {1, retro::SaveManager::kAutosaveSlot}) {
    if (const auto info = saves.Peek(slot); info && (!best || info->savedAt > bestAt)) {
      best = slot;
      bestAt = info->savedAt;
    }
  }
  return best;
}

// RetroEngine [--scene scenes/x.json]: sin argumentos empieza en la pantalla de inicio si ya se
// ha llegado alguna vez a la feria, y si no en la primera escena; con --scene, directamente
// jugando en esa escena (para probar).
int main(int argc, char** argv) {
  std::optional<std::string> sceneArg;
  bool forceWindowed = false; // --windowed: en ventana aunque los ajustes digan pantalla completa
  for (int i = 1; i < argc; ++i) {
    if (std::string(argv[i]) == "--scene" && i + 1 < argc)
      sceneArg = argv[i + 1];
    if (std::string(argv[i]) == "--windowed")
      forceWindowed = true;
  }

  try {
    retro::EngineConfig config;
    config.title = "Festum Venti";
    retro::RetroEngine engine(config);
    retro::RetroCamera camera;

    retro::SaveManager saves(retro::SaveManager::DefaultDirectory("RetroEngine"));
    const std::filesystem::path settingsFile = saves.Directory().parent_path() / "ajustes.json";
    retro::Settings settings = retro::LoadSettings(settingsFile);
    if (forceWindowed)
      settings.fullscreen = false; // solo esta vez (no se guarda si no se cambia un ajuste)

    Screen screen = !sceneArg && LatestSaveReachedTitle(saves, LatestSave(saves))
                        ? Screen::Title
                        : Screen::Playing;
    const std::string primera = sceneArg                  ? *sceneArg
                                : screen == Screen::Title ? kTitleScene
                                                          : kFirstScene;
    retro::SceneManager scenes(engine, camera, {primera});
    scenes.LoadNow(0);
    scenes.FadeInFromBlack();

    retro::Player player;
    retro::GameState state;
    retro::DialogueSystem dialogues(engine.Assets().Root());
    retro::ActionRunner runner(scenes, player, state, engine.Audio(), dialogues);

    std::vector<retro::WorldBox> colliders;
    std::vector<retro::Action> firedActions;
    std::uint64_t seenGeneration = scenes.Generation();
    bool firstPerson = true;
    std::optional<Focus> dialogueFocus; // a quien se mira mientras dura un dialogo
    // Montado en algo (ride): el asiento va en coordenadas de la entidad, asi gira y sube con ella.
    struct Riding {
      std::string entity;
      Vector3 local;
      Vector3 exit;
      float seconds = 0.0f;
      std::string flag;
      std::string after; // dialogo al terminar
      bool locked = false; // no se baja con E: hay que esperar a 'seconds'
      float age = 0.0f;
    };
    std::optional<Riding> riding;
    // Tiro al blanco en curso (shooting): las dianas acertadas vuelven a salir al rato.
    struct Gallery {
      std::string prefix;
      float left = 0.0f;
      std::string flag;
      std::string after; // dialogo al terminar
      int score = 0;
      std::vector<std::pair<retro::Entity*, float>> hidden; // diana y segundos para volver
    };
    std::optional<Gallery> gallery;
    float zoom = 0.0f;                  // grados que se ha estrechado la vista ahora
    LetterSounds letterSounds;
    float dialogueAge = 0.0f; // tiempo en la pagina actual
    unsigned pageId = 0;
    bool showColliders = false;
    bool showHelp = false;
    retro::FootstepTracker footsteps;

    double playTime = 0.0f;
    std::optional<retro::SaveData> pendingLoad;
    const auto initialFlags = state.Flags();
    const std::uint64_t initialGeneration = scenes.Generation();

    std::string interactPrompt;

    auto spawnPlayer = [&]() {
      if (const retro::Scene* s = scenes.Current()) {
        player.Spawn(s->spawn.position, s->spawn.yaw);
        footsteps.Reset(s->spawn.position);
      }
    };
    spawnPlayer();

    auto snapshot = [&]() {
      retro::SaveData d;
      d.scene = scenes.CurrentPath();
      d.position = player.Feet();
      d.yaw = player.Yaw();
      d.pitch = player.Pitch();
      d.playTime = playTime;
      d.flags = state.Flags();
      return d;
    };

    auto saveTo = [&](int slot, const char* confirmation, bool allowDuringTransition = false) {
      if (scenes.Transitioning() && !allowDuringTransition) {
        if (confirmation)
          state.ShowMessage("No se puede guardar durante un cambio de escena", 2.5f);
        return;
      }
      try {
        saves.Write(slot, snapshot());
        if (confirmation)
          state.ShowMessage(confirmation, 2.0f);
      } catch (const std::exception& e) {
        std::cerr << "Guardado: " << e.what() << '\n';
        if (confirmation)
          state.ShowMessage("Error al guardar (ver consola)", 3.5f);
      }
    };

    auto loadFrom = [&](int slot) {
      if (scenes.Transitioning() || pendingLoad)
        return;
      try {
        bool backup = false;
        retro::SaveData data = saves.Read(slot, &backup);
        scenes.GoToPath(data.scene, data.position, data.yaw);
        pendingLoad = std::move(data);
        if (backup)
          std::cerr << "Carga: el guardado estaba danado; se uso la copia de seguridad\n";
      } catch (const std::exception& e) {
        std::cerr << "Carga: " << e.what() << '\n';
        state.ShowMessage("No se pudo cargar la partida (ver consola)", 3.5f);
      }
    };

    // --- Ajustes ---
    ApplySettings(settings, engine, player);
    auto saveSettings = [&]() {
      try {
        retro::SaveSettings(settingsFile, settings);
      } catch (const std::exception& e) {
        std::cerr << "Ajustes: " << e.what() << '\n';
      }
    };

    // --- Menus: pantalla de inicio, pausa y configuracion ---
    retro::Menu menu;
    Page page = Page::Main;
    // Mientras se va a otra escena desde un menu (jugar, volver al inicio): la pantalla a la que
    // se pasa cuando llegue. El menu no se ve ni responde mientras tanto.
    std::optional<Screen> leaving;
    bool arrival = false; // la pantalla de inicio vino de jugar (title_screen), no del arranque

    auto mainItems = [&]() -> std::vector<retro::Menu::Item> {
      if (screen == Screen::Paused && state.Get(kTitleFlag) != 0)
        return {{kResume, "Seguir jugando"}, {kSettings, "Configuracion"},
                {kToTitle, "Salir al menu"}, {kQuit, "Salir del juego"}};
      if (screen == Screen::Paused) // aun no hay pantalla de inicio a la que volver
        return {{kResume, "Seguir jugando"}, {kSettings, "Configuracion"},
                {kQuit, "Salir del juego"}};
      if (arrival) // se llego jugando: se sigue desde aqui
        return {{kPlay, "Jugar"}, {kSettings, "Configuracion"}, {kQuit, "Salir"}};
      return {{kContinue, "Continuar", "", LatestSave(saves).has_value()},
              {kNewGame, "Nueva partida"}, {kSettings, "Configuracion"}, {kQuit, "Salir"}};
    };
    auto settingsItems = [&]() -> std::vector<retro::Menu::Item> {
      static constexpr const char* kFilters[] = {"Pixeles", "Nitida", "Suave"};
      settings.fullscreen = IsWindowState(FLAG_BORDERLESS_WINDOWED_MODE); // tambien con F11
      return {{kVolume, "Volumen", TextFormat("%d%%", static_cast<int>(std::lround(settings.volume * 100.0f)))},
              {kSensitivity, "Raton", std::to_string(settings.sensitivity)},
              {kFullscreen, "Pantalla completa", settings.fullscreen ? "Si" : "No"},
              {kFilter, "Imagen", kFilters[static_cast<int>(settings.filter)]},
              {kEffects, "Grano y vineta", settings.effects ? "Si" : "No"},
              {kBack, "Volver"}};
    };
    auto openPage = [&](Page p) {
      page = p;
      menu.SetItems(p == Page::Main ? mainItems() : settingsItems());
      menu.Select(0);
    };
    auto changeSetting = [&](int id, int dir) {
      switch (id) {
      case kVolume:
        settings.volume = std::clamp(std::round(settings.volume * 10.0f + dir) / 10.0f, 0.0f, 1.0f);
        break;
      case kSensitivity:
        settings.sensitivity = std::clamp(settings.sensitivity + dir, 1, 10);
        break;
      case kFullscreen:
        settings.fullscreen = !settings.fullscreen;
        break;
      case kFilter:
        settings.filter = static_cast<retro::FilterMode>((static_cast<int>(settings.filter) + 3 + dir) % 3);
        break;
      case kEffects:
        settings.effects = !settings.effects;
        break;
      default:
        return;
      }
      ApplySettings(settings, engine, player);
      saveSettings();
      menu.SetItems(settingsItems());
    };

    // Pantalla de inicio: la camara da vueltas alrededor del "target" de la camara de la escena,
    // a la distancia y altura a las que la escena la pone. Si se llega jugando (title_screen,
    // 'arrival'), "Jugar" lleva la camara volando al punto de inicio de la escena ('glide').
    Vector3 orbitCenter{0.0f, 0.0f, 0.0f};
    float orbitRadius = 12.0f, orbitHeight = 3.0f, orbitAngle = 0.0f, titleTime = 0.0f;
    float menuAge = 0.0f;      // segundos con el menu a la vista (aparece despacio)
    bool titleMusic = false;   // ya suena el vals con percusion
    struct Glide {
      Vector3 fromEye;
      Vector3 fromLook;
      float t = 0.0f;
    };
    std::optional<Glide> glide;
    auto startTitle = [&]() {
      orbitCenter = camera.Target();
      const Vector3 d = Vector3Subtract(camera.Position(), orbitCenter);
      orbitRadius = std::max(std::hypot(d.x, d.z), 4.0f);
      orbitHeight = d.y;
      orbitAngle = std::atan2(d.x, d.z);
      titleMusic = false; // cambia cuando el menu aparezca (la de la escena suena en el negro)
      menuAge = 0.0f;
      player.SetActive(false);
      openPage(Page::Main);
    };
    auto orbit = [&](float dt) {
      if (!scenes.Transitioning()) {
        menuAge += dt;
        if (!titleMusic) {
          engine.Audio().PlayMusic(kTitleMusic, 0.55f, 1.5f); // en lugar de la de la escena
          titleMusic = true;
        }
      }
      if (glide) { // "Jugar": de la vuelta al tiovivo a los ojos del jugador
        glide->t = std::min(glide->t + dt / 3.0f, 1.0f);
        const float k = glide->t * glide->t * (3.0f - 2.0f * glide->t);
        const Vector3 eye = player.Eye();
        const float yaw = player.Yaw() * DEG2RAD;
        const Vector3 look = Vector3Add(eye, {-std::sin(yaw), 0.0f, -std::cos(yaw)});
        camera.SetPosition(Vector3Lerp(glide->fromEye, eye, k));
        camera.LookAt(Vector3Lerp(glide->fromLook, look, k));
        scenes.SetFocus(player.Feet());
        if (glide->t >= 1.0f) {
          glide.reset();
          screen = Screen::Playing;
          firstPerson = true;
          player.SetActive(true);
        }
        return;
      }
      titleTime += dt;
      orbitAngle += dt * 0.07f;
      const Vector3 eye{orbitCenter.x + std::sin(orbitAngle) * orbitRadius,
                        orbitCenter.y + orbitHeight + 0.4f * std::sin(titleTime * 0.25f),
                        orbitCenter.z + std::cos(orbitAngle) * orbitRadius};
      camera.SetPosition(eye);
      camera.LookAt(orbitCenter);
      scenes.SetFocus({eye.x, 0.0f, eye.z}); // los espantapajaros siguen a la camara
    };

    auto pause = [&]() {
      screen = Screen::Paused;
      menuAge = 1.0f; // la pausa sale de golpe
      interactPrompt.clear();
      player.SetActive(false);
      openPage(Page::Main);
    };
    auto resume = [&]() {
      screen = Screen::Playing;
      player.SetActive(firstPerson);
    };

    auto runMenu = [&]() {
      const Vector2 m = engine.WindowToInternal(GetMousePosition());
      const float k = retro::Ui::kRefWidth / static_cast<float>(engine.Config().internalWidth);
      const retro::Menu::Result r = menu.Update({m.x * k, m.y * k});
      switch (r.event) {
      case retro::Menu::Event::None:
        break;
      case retro::Menu::Event::Moved:
        engine.Audio().PlaySfx("audio/letra_1.wav", 0.5f);
        break;
      case retro::Menu::Event::Left:
      case retro::Menu::Event::Right:
        engine.Audio().PlaySfx("audio/letra_2.wav", 0.6f);
        changeSetting(r.id, r.event == retro::Menu::Event::Left ? -1 : 1);
        break;
      case retro::Menu::Event::Back:
        if (page == Page::Settings)
          openPage(Page::Main);
        else if (screen == Screen::Paused)
          resume();
        break;
      case retro::Menu::Event::Accept:
        engine.Audio().PlaySfx("audio/letra_3.wav", 0.8f);
        switch (r.id) {
        case kPlay: {
          // Se sigue jugando aqui mismo: la camara vuela al punto de inicio y vuelve la musica
          // de la escena (sin percusion).
          glide = Glide{camera.Position(), camera.Target()};
          arrival = false;
          if (const retro::Scene* s = scenes.Current())
            engine.Audio().PlayMusic(s->audio.music, s->audio.musicVolume, 2.5f);
          break;
        }
        case kContinue:
          if (const auto slot = LatestSave(saves)) {
            loadFrom(*slot);
            if (pendingLoad)
              leaving = Screen::Playing;
          }
          break;
        case kNewGame:
          state.Clear();
          playTime = 0.0;
          scenes.GoToPath(kFirstScene);
          leaving = Screen::Playing;
          break;
        case kSettings:
          openPage(Page::Settings);
          break;
        case kQuit:
          engine.Quit();
          break;
        case kResume:
          resume();
          break;
        case kToTitle:
          saveTo(retro::SaveManager::kAutosaveSlot, nullptr);
          state.ClearMessage();
          dialogueFocus.reset();
          scenes.GoToPath(kTitleScene);
          leaving = Screen::Title;
          break;
        case kBack:
          openPage(Page::Main);
          break;
        default:
          break;
        }
        break;
      }
    };

    // Al llegar a una escena: si se venia de un menu, se cambia de pantalla; en la de inicio la
    // camara empieza a dar vueltas; jugando, se coloca al jugador (y se aplica la partida cargada
    // o se autoguarda).
    auto onSceneChanged = [&]() {
      zoom = 0.0f; // la escena nueva ya puso su campo de vision
      player.SetRooted(false); // lock_movement dura solo lo que la escena
      riding.reset();          // y lo mismo ir montado o el tiro al blanco
      gallery.reset();
      if (leaving) {
        screen = *std::exchange(leaving, std::nullopt);
        if (screen == Screen::Playing) {
          firstPerson = true;
          player.SetActive(true);
        }
      }
      if (screen == Screen::Title) {
        startTitle();
        if (arrival) { // se llego jugando: el jugador ya esta aqui (y se guarda aqui)
          spawnPlayer();
          saveTo(retro::SaveManager::kAutosaveSlot, nullptr, true);
        }
        return;
      }
      spawnPlayer();
      if (pendingLoad) {
        state.Clear();
        state.SetFlags(pendingLoad->flags);
        player.SetPitch(pendingLoad->pitch);
        playTime = pendingLoad->playTime;
        pendingLoad.reset();
        state.ShowMessage("Partida cargada", 2.0f);
      } else {
        saveTo(retro::SaveManager::kAutosaveSlot, nullptr, true);
      }
    };

    if (screen == Screen::Title) {
      startTitle();
    } else {
      player.SetActive(true);
      if (saves.Exists(1) || saves.Exists(retro::SaveManager::kAutosaveSlot))
        state.ShowMessage("Hay una partida guardada.\nF10: cargar    Shift+F10: autoguardado", 6.0f);
    }

    engine.Run(
        [&](float dt) {
          // --- Fotos (F12): el motor la saca al final del fotograma; el aviso, en el siguiente ---
          if (const auto saved = engine.TakeScreenshotResult())
            state.ShowMessage(*saved ? "Foto guardada" : "No se pudo guardar la foto (ver consola)",
                              2.0f);
          if (IsKeyPressed(KEY_F12)) {
            try {
              const std::filesystem::path dir = PhotosDirectory(saves);
              std::filesystem::create_directories(dir);
              const std::filesystem::path file = NewPhotoPath(dir);
              std::cout << "Foto: " << file.string() << std::endl;
              engine.RequestScreenshot(file.string());
            } catch (const std::exception& e) {
              std::cerr << "Foto: " << e.what() << '\n';
              state.ShowMessage("No se pudo guardar la foto (ver consola)", 2.5f);
            }
          }

          // --- Menus (inicio y pausa) ---
          if (screen != Screen::Playing && !leaving && !glide && !scenes.Transitioning() &&
              menuAge > 0.4f)
            runMenu();
          else if (screen == Screen::Playing && IsKeyPressed(KEY_ESCAPE) && !scenes.Transitioning())
            pause();
          if (screen == Screen::Paused && !leaving)
            return; // en pausa el mundo se queda quieto
          if (screen != Screen::Playing) {
            // la pantalla de inicio de fondo, o el fundido al salir de un menu
            if (screen == Screen::Title)
              orbit(dt);
            scenes.Update(dt);
            if (scenes.Generation() != seenGeneration) {
              seenGeneration = scenes.Generation();
              onSceneChanged();
            } else if (leaving && !scenes.Transitioning()) {
              leaving.reset(); // no se pudo cambiar de escena: el menu vuelve
              if (std::exchange(pendingLoad, std::nullopt))
                state.ShowMessage("No se pudo cargar la escena guardada (ver consola)", 3.5f);
            }
            if (retro::Scene* s = scenes.CurrentMut())
              s->RefreshVisibility(state);
            scenes.UpdateAudio(dt);
            return;
          }

          playTime += dt;

          if (IsKeyPressed(KEY_F9))
            saveTo(1, "Partida guardada");

          if (IsKeyPressed(KEY_F10)) {
            const bool shift = IsKeyDown(KEY_LEFT_SHIFT) || IsKeyDown(KEY_RIGHT_SHIFT);
            loadFrom(shift ? retro::SaveManager::kAutosaveSlot : 1);
          }

          if (IsKeyPressed(KEY_TAB)) {
            if (IsKeyDown(KEY_LEFT_SHIFT) || IsKeyDown(KEY_RIGHT_SHIFT)) {
              scenes.Previous();
            } else {
              scenes.Next();
            }
          }

          if (IsKeyPressed(KEY_F5)) {
            try {
              if (dialogues.Active())
                state.ClearMessage();
              dialogues.Reload(); // tambien se releen los dialogos
              scenes.Reload();
              std::cout << "Escena recargada: " << scenes.CurrentPath() << '\n';
            } catch (const std::exception& e) {
              std::cerr << "Error al recargar: " << e.what() << '\n';
            }
          }

          if (IsKeyPressed(KEY_F6)) {
            state.Clear();
            std::cout << "Estado de la partida reiniciado (F5 para recargar la escena)\n";
          }

          if (IsKeyPressed(KEY_F1))
            showHelp = !showHelp;

          if (IsKeyPressed(KEY_F2)) {
            firstPerson = !firstPerson;
            player.SetActive(firstPerson);
            if (!firstPerson) {
              const Vector3 f = player.Feet();
              camera.OrbitAround({f.x, f.y + 1.0f, f.z}, 8.0f);
            }
          }
          if (IsKeyPressed(KEY_F3))
            showColliders = !showColliders;

          scenes.SetFocus(player.Feet());
          scenes.Update(dt);
          state.Update(dt);
          letterSounds.Update(state, engine.Audio());

          if (scenes.Generation() != seenGeneration) {
            seenGeneration = scenes.Generation();
            onSceneChanged();
          } else if (pendingLoad && !scenes.Transitioning()) {
            pendingLoad.reset();
            state.ShowMessage("No se pudo cargar la escena guardada (ver consola)", 3.5f);
          }

          if (retro::Scene* s = scenes.CurrentMut())
            s->RefreshVisibility(state);

          colliders.clear();
          if (const retro::Scene* s = scenes.Current())
            s->CollectColliders(colliders);

          // Dialogo: un mensaje que salio de una interaccion, o un dialogo abierto. Mientras se
          // lee, el jugador se queda quieto (mirando a quien habla si se sabe quien es); un
          // anuncio suelto (trigger, sistema) no lo retiene. Una mirada forzada (look_at, scare)
          // tampoco suelta al jugador hasta que la camara ha llegado, aunque el mensaje ya se
          // haya ido: si no, se podria andar a medio giro y romper la escena.
          dialogues.Update(state);
          if (dialogueFocus && !state.HasMessage() && dialogueFocus->settled)
            dialogueFocus.reset();
          const bool reading = dialogueFocus.has_value() || dialogues.Active();
          if (state.MessageId() != pageId) { // cada pagina nueva da su respiro
            pageId = state.MessageId();
            dialogueAge = 0.0f;
          }
          dialogueAge = reading ? dialogueAge + dt : 0.0f;
          player.SetFrozen(reading);

          bool eUsed = false; // E ya sirvio para algo este fotograma (bajar, disparar)
          if (firstPerson) {
            if (riding) {
              // montado: los pies siguen el asiento (que gira y sube con la entidad) y solo se
              // mira; E (con un respiro) o el tiempo bajan en 'exit'
              riding->age += dt;
              retro::Scene* s = scenes.CurrentMut();
              retro::Entity* e = s ? s->Find(riding->entity) : nullptr;
              if (e)
                player.PlaceFeet(Vector3Transform(riding->local, e->transform.ToMatrix()));
              player.UpdateLook();
              const bool timeUp = riding->seconds > 0.0f && riding->age >= riding->seconds;
              const bool getOff =
                  IsKeyPressed(KEY_E) && riding->age > 0.8f && !reading && !riding->locked;
              if (!e || timeUp || getOff) {
                eUsed = getOff;
                player.PlaceFeet(riding->exit);
                if (!riding->flag.empty())
                  state.Set(riding->flag, 0);
                if (!riding->after.empty())
                  runner.Run({retro::action::StartDialogue{riding->after}});
                riding.reset();
              }
            } else if (!scenes.Transitioning()) {
              player.Update(dt, colliders);
            }
            if (dialogueFocus) {
              const float left = player.TurnTowards(dialogueFocus->point, dt, dialogueFocus->rate);
              dialogueFocus->age += dt;
              dialogueFocus->settled = left < 2.0f || dialogueFocus->age > 6.0f; // 6 s: por si acaso
            }
            player.ApplyToCamera(camera);
          } else {
            camera.UpdateOrbit(dt);
            if (dialogueFocus)
              dialogueFocus->settled = true; // con la camara libre no hay giro que esperar
          }

          footsteps.Update(player.Feet(), player.Grounded(), IsKeyDown(KEY_LEFT_SHIFT),
                           firstPerson && !scenes.Transitioning(), engine.Audio());
          scenes.UpdateAudio(dt);

          // --- Tiro al blanco ---
          if (gallery && !reading) {
            gallery->left -= dt;
            for (auto it = gallery->hidden.begin(); it != gallery->hidden.end();) {
              it->second -= dt;
              if (it->second <= 0.0f) {
                it->first->visible = true;
                it = gallery->hidden.erase(it);
              } else {
                ++it;
              }
            }
            if (IsMouseButtonPressed(MOUSE_BUTTON_LEFT) || IsKeyPressed(KEY_E)) {
              eUsed = true;
              engine.Audio().PlaySfx("audio/corcho.wav", 0.8f);
              const Vector3 origin = camera.Position();
              const Vector3 dir = Vector3Normalize(Vector3Subtract(camera.Target(), origin));
              retro::Scene* s = scenes.CurrentMut();
              if (retro::Entity* t = s ? s->FindTarget(origin, dir, gallery->prefix) : nullptr) {
                t->visible = false;
                gallery->hidden.emplace_back(t, 1.4f);
                ++gallery->score;
                engine.Audio().PlaySfx("audio/acierto.wav", 0.7f);
              }
            }
            if (gallery->left <= 0.0f) {
              for (auto& [t, _] : gallery->hidden)
                t->visible = true;
              state.Set(gallery->flag, gallery->score);
              player.SetRooted(false);
              if (!gallery->after.empty())
                runner.Run({retro::action::StartDialogue{gallery->after}});
              else
                state.ShowMessage(TextFormat("Tiempo! %d aciertos.", gallery->score), 3.0f);
              gallery.reset();
            }
          }

          // --- Triggers ---
          if (firstPerson && !scenes.Transitioning() && !riding) {
            if (retro::Scene* s = scenes.CurrentMut()) {
              const retro::PlayerSettings& cfg = player.Settings();
              firedActions.clear();
              s->UpdateTriggers(player.Feet(), cfg.radius, cfg.height, state, firedActions);
              // lo que se ve: entidades con "seen" en el centro de la vista
              const Vector3 eye = camera.Position();
              s->UpdateSight(eye, Vector3Normalize(Vector3Subtract(camera.Target(), eye)), state,
                             firedActions);
              runner.Run(firedActions);
            }
          }

          // --- Interaccion (E) ---
          interactPrompt.clear();
          if (firstPerson && !scenes.Transitioning() && !eUsed && !riding && !gallery) {
            if (retro::Scene* s = scenes.CurrentMut()) {
              const Vector3 origin = camera.Position();
              const Vector3 dir = Vector3Normalize(Vector3Subtract(camera.Target(), origin));
              const auto hit = s->FindInteractable(origin, dir, state);
              if (reading) {
                // E durante un dialogo: si aun se esta escribiendo, lo muestra entero; si no,
                // pasa de pagina o lo cierra (con un respiro, para no saltarlo sin leer, y no
                // antes de que la camara termine de girar). Las acciones del final se ejecutan
                // al cerrar (pueden abrir otro dialogo).
                const bool turning = dialogueFocus && !dialogueFocus->settled;
                if (IsKeyPressed(KEY_E)) {
                  if (!state.FullyShown()) {
                    state.RevealAll();
                  } else if (dialogueAge > 0.3f && !turning) {
                    if (dialogues.Active()) {
                      const std::vector<retro::Action> end = dialogues.Advance(state);
                      runner.Run(end);
                    } else {
                      state.ClearMessage();
                    }
                  }
                }
              } else if (hit.entity) {
                interactPrompt = "E: " + hit.entity->interact->prompt;
                if (IsKeyPressed(KEY_E)) {
                  firedActions.clear();
                  s->Interact(*hit.entity, state, firedActions);
                  runner.Run(firedActions);
                  if (Speaks(firedActions) && state.HasMessage())
                    dialogueFocus = Focus{DialogueFocus(*hit.entity)};
                }
              }
            }
          }

          // look_at o scare (de un trigger, una interaccion o el final de un dialogo): a donde
          // mirar mientras dure lo que se esta leyendo. Si no hay nada que leer, no retiene al
          // jugador.
          if (runner.TakeTitleRequest()) { // title_screen: al llegar, la pantalla de inicio
            leaving = Screen::Title;
            arrival = true;
          }
          if (auto r = runner.TakeRide()) { // ride: el asiento, en coordenadas de la entidad
            retro::Scene* s = scenes.CurrentMut();
            if (retro::Entity* e = s ? s->Find(r->entity) : nullptr) {
              const Matrix inv = MatrixInvert(e->transform.ToMatrix());
              riding = Riding{r->entity, Vector3Transform(r->seat, inv),
                              r->exit.value_or(player.Feet()), r->seconds, r->flag, r->after,
                              r->locked, 0.0f};
              if (!r->flag.empty())
                state.Set(r->flag, 1);
            } else {
              std::cerr << "ride: entidad inexistente '" << r->entity << "'\n";
            }
          }
          if (auto g = runner.TakeShooting()) { // shooting: al puesto, quieto, a disparar
            player.Spawn(g->position, g->yaw);
            player.SetRooted(true);
            gallery = Gallery{g->targets, g->seconds, g->flag, g->after, 0, {}};
          }
          if (auto look = runner.TakeShot(); look && state.HasMessage()) {
            retro::Scene* s = scenes.CurrentMut();
            retro::Entity* target = s && !look->entity.empty() ? s->Find(look->entity) : nullptr;
            if (target)
              dialogueFocus = Focus{DialogueFocus(*target), look->speed, look->fov};
            else if (look->point)
              dialogueFocus = Focus{*look->point, look->speed, look->fov};
            else
              std::cerr << "look_at/scare: entidad inexistente '" << look->entity << "'\n";
          }

          // Acercamiento de scare: la vista se estrecha hacia su "fov" y vuelve al soltar.
          {
            const float sceneFov = camera.Fov() + zoom;
            const float want =
                dialogueFocus && dialogueFocus->fov > 0.0f
                    ? std::max(0.0f, sceneFov - dialogueFocus->fov)
                    : 0.0f;
            const float rate = dialogueFocus ? dialogueFocus->rate : 4.0f;
            zoom += (want - zoom) * std::min(1.0f, dt * rate);
            camera.SetFov(sceneFov - zoom);
          }

          if (retro::Scene* s = scenes.CurrentMut())
            s->RefreshVisibility(state);

          HandleDebugKeys(engine);
        },

        [&]() {
          camera.Begin();
          scenes.Draw();

          if (const retro::Scene* s = scenes.Current()) {
            retro::ShaderScope lines(engine.Retro().Unlit()->Get());
            for (const retro::Trigger& t : s->triggers) {
              if (t.marker)
                retro::DrawPortalFrame(t.box, t.color);
              if (showColliders)
                retro::DrawWorldBoxWires(t.box, {80, 255, 120, 255});
            }
          }

          if (showColliders) {
            for (const retro::WorldBox& b : colliders) {
              retro::DrawWorldBoxWires(b, {255, 80, 80, 255});
            }
          }
          camera.End();

          scenes.DrawFade();
        },

        [&](retro::Ui& ui) {
          const float w = ui.Width();
          const float h = ui.Height();

          if (screen == Screen::Playing && firstPerson && !dialogueFocus && !dialogues.Active() &&
              !scenes.Transitioning()) {
            const Color c = interactPrompt.empty() ? RAYWHITE : YELLOW;
            ui.Rect(w * 0.5f - 1.0f, h * 0.5f - 1.0f, 2.0f, 2.0f, c); // mira
          }
          if (!interactPrompt.empty()) {
            const float tw = ui.TextWidth(interactPrompt, 10.0f);
            ui.Text(interactPrompt, (w - tw) * 0.5f, h * 0.5f + 8.0f, 10.0f, YELLOW);
          }

          if (showHelp) {
            ui.Text("1 jitter 2 color 3 dither 4 niebla 6 luces", 4, 3, 8, RAYWHITE);
            ui.Text("F5 recargar  F6 reset  F7 filtro  F8 acabado M sonido", 4, 13, 8, RAYWHITE);
            ui.Text(firstPerson ? "WASD mover  Shift correr  Espacio saltar  E usar"
                                : "Arrastrar/flechas: orbitar  Rueda: zoom",
                    4, 23, 8, RAYWHITE);
            ui.Text("F9 guardar  F10 cargar  Shift+F10 autoguardado", 4, 43, 8, RAYWHITE);
            ui.Text("Tab escena  F1 ayuda  F2 camara  F3 colisiones", 4, 33, 8, RAYWHITE);

            ui.Text(TextFormat("Escena %d/%d: %s", static_cast<int>(scenes.Index()) + 1,
                               static_cast<int>(scenes.Count()), scenes.CurrentPath().c_str()),
                    4, h - 12.0f, 8.0f, RAYWHITE);
          }

          if (screen == Screen::Playing && riding && !riding->locked) {
            const char* hint = "E: bajar";
            ui.Text(hint, (w - ui.TextWidth(hint, 9.0f)) * 0.5f, h - 16.0f, 9.0f,
                    Color{230, 214, 180, 220});
          }
          if (screen == Screen::Playing && gallery) {
            const std::string score = TextFormat("ACIERTOS %d    %02d", gallery->score,
                                                 static_cast<int>(std::ceil(gallery->left)));
            ui.Text(score, (w - ui.TextWidth(score, 12.0f)) * 0.5f, 10.0f, 12.0f,
                    Color{240, 210, 120, 255});
          }
          if (const auto card = scenes.CurrentCard())
            DrawCard(ui, card->text, card->alpha);
          if (state.HasMessage())
            DrawSubtitle(ui, state.MessageText(), state.VisibleBytes(), state.MessageSpeaker(),
                         dialogues.Active());

          // --- Menus ---
          if (screen == Screen::Playing || leaving || glide || scenes.Transitioning())
            return;
          // aparece despacio (la primera vez, al salir del negro, el titulo se descubre)
          const float a = std::clamp(menuAge / 1.5f, 0.0f, 1.0f);
          auto fade = [a](Color c) {
            c.a = static_cast<unsigned char>(static_cast<float>(c.a) * a);
            return c;
          };
          const Color cream = fade({232, 214, 180, 255});
          const Color dim = fade({170, 150, 130, 255});
          float top = 112.0f;
          if (screen == Screen::Title) {
            // la feria detras, oscurecida para que se lea: sin bandas, un velo que se espesa
            // detras del titulo y del menu y se difumina hacia los bordes (franjas de 2 px)
            for (float y = 0.0f; y < h; y += 2.0f) {
              const auto bump = [y](float center, float width) {
                const float d = (y - center) / width;
                return std::exp(-d * d);
              };
              const float veil = 55.0f + 75.0f * bump(42.0f, 34.0f) + 70.0f * bump(142.0f, 46.0f);
              ui.Rect(0.0f, y, w, 2.0f,
                      fade({8, 6, 14, static_cast<unsigned char>(std::min(veil, 150.0f))}));
            }
            const float tw = ui.TextWidth(kGameTitle, 22.0f);
            ui.Text(kGameTitle, (w - tw) * 0.5f, 28.0f, 22.0f, cream);
            const char* sub = "Valdemora, 1702";
            ui.Text(sub, (w - ui.TextWidth(sub, 8.0f)) * 0.5f, 54.0f, 8.0f, dim);
          } else {
            ui.Rect(0.0f, 0.0f, w, h, Color{0, 0, 0, 150});
            const float tw = ui.TextWidth("PAUSA", 18.0f);
            ui.Text("PAUSA", (w - tw) * 0.5f, 50.0f, 18.0f, cream);
          }
          if (page == Page::Settings) {
            const char* head = "Configuracion";
            ui.Text(head, (w - ui.TextWidth(head, 9.0f)) * 0.5f, top - 14.0f, 9.0f, dim);
            top += 4.0f;
          }
          menu.Draw(ui, top, a);
          const char* hint = page == Page::Settings
                                 ? "W/S elegir   A/D cambiar   Esc volver"
                                 : "W/S elegir   E o Enter aceptar";
          ui.Text(hint, (w - ui.TextWidth(hint, 7.0f)) * 0.5f, h - 14.0f, 7.0f,
                  fade({130, 120, 112, 255}));
        });

    if (screen != Screen::Title &&
        (scenes.Generation() != initialGeneration || state.Flags() != initialFlags)) {
      saveTo(retro::SaveManager::kAutosaveSlot, nullptr, true);
    }
  } catch (const std::exception& e) {
    std::cerr << "Error fatal: " << e.what() << '\n';
    return 1;
  }
  return 0;
}