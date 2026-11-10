#include <raylib.h>
#include <raymath.h>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <ctime>
#include <exception>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <optional>
#include <string>
#include <tuple>
#include <unordered_map>
#include <variant>
#include <vector>

#include <nlohmann/json.hpp>

#include "audio/AudioSystem.hpp"
#include "audio/Footsteps.hpp"
#include "core/RetroEngine.hpp"
#include "game/GameConfig.hpp"
#include "game/GameState.hpp"
#include "game/Localization.hpp"
#include "game/SaveGame.hpp"
#include "game/Settings.hpp"
#include "graphics/DebugDraw.hpp"
#include "graphics/RetroCamera.hpp"
#include "graphics/ShaderScope.hpp"
#include "physics/Collision.hpp"
#include "player/Player.hpp"
#include "ui/Garbled.hpp"
#include "ui/Menu.hpp"
#include "ui/Ui.hpp"
#include "world/ActionRunner.hpp"
#include "world/Weather.hpp"
#include "world/Dialogue.hpp"
#include "world/SceneManager.hpp"

// El runtime de todos los juegos: el bucle, la pantalla de inicio, los menus, las partidas, la intro
// y las mecanicas (ojos cerrados, camara de fotos, tiro al blanco...). Lo que hace de un juego ese
// juego esta en su game.json (ver game/GameConfig.hpp); CMake (retro_add_game) compila este archivo
// una vez por juego, con RETRO_GAME_DIR = la carpeta del juego junto al ejecutable.
//
// Las herramientas de depuracion (teclas F1-F10, Tab, 1-6, M y las opciones --scene, --pos...)
// solo existen en <exe>Debug.exe (CMake define RETRO_DEBUG_TOOLS). <exe>.exe es el juego tal
// cual: menus, partidas y nada mas.
#ifdef RETRO_DEBUG_TOOLS
constexpr bool kDebugTools = true;
#else
constexpr bool kDebugTools = false;
#endif

// 1 jitter, 2 color, 3 dither, 4 niebla, 6 luces, F7 filtro, F8 acabado, M silencio (depuracion).
[[maybe_unused]] static void HandleDebugKeys(retro::RetroEngine& engine) {
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

// Los nombres de lo que se puede llevar en la mano (bandera "lleva"): el archivo "items" del
// game.json, { "1": "Una carta sin abrir", ... }. Sin archivo, sin nombres.
static std::unordered_map<int, std::string> LoadItemNames(const std::string& file) {
  std::unordered_map<int, std::string> names;
  if (file.empty())
    return names;
  std::ifstream in(file);
  if (!in)
    return names;
  try {
    const nlohmann::json j = nlohmann::json::parse(in);
    for (const auto& item : j.items())
      names[std::stoi(item.key())] = item.value().get<std::string>();
  } catch (const std::exception& e) {
    std::cerr << "[objetos] " << e.what() << '\n';
  }
  return names;
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
static Vector3 DialogueFocus(retro::Entity& e) { return retro::LookPoint(e); }

// Si las acciones dicen algo (un mensaje o un dialogo): entonces la camara mira a quien habla.
static bool Speaks(const std::vector<retro::Action>& actions) {
  return std::any_of(actions.begin(), actions.end(), [](const retro::Action& a) {
    return std::holds_alternative<retro::action::Message>(a) ||
           std::holds_alternative<retro::action::StartDialogue>(a);
  });
}

using retro::lang::Tr; // los textos se escriben en el idioma de origen; Tr da el elegido

// La pantalla de inicio y la intro salen del game.json (GameConfig). Una pantalla de inicio con
// "orbit" da vueltas a su punto en su escena y en las que empiezan por sus prefijos; en las demas,
// al "target" de la camara de la escena.
using TitleSet = retro::GameConfig::TitleScreen;
using IntroShot = retro::GameConfig::IntroShot;
static bool OrbitsHere(const TitleSet& t, const std::string& scene) {
  return t.fixedOrbit && retro::GameConfig::Here(t, scene);
}

enum class Screen { Title, Playing, Paused, Intro };
// Slots: la lista de partidas (para empezar una nueva o para cargar); Confirm: borrar una partida
// para empezar otra en su ranura.
enum class Page { Main, Settings, Slots, Confirm };
enum Option : int {
  kPlay, kContinue, kNewGame, kLoadGame, kSettings, kQuit, kResume, kToTitle,
  kVolume, kSensitivity, kFullscreen, kFilter, kEffects, kLanguage, kBack, kConfirm,
  kSlot0 = 100 // kSlot0 + n: la partida n
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

// La partida guardada mas reciente: de ella sale la ciudad de la pantalla de inicio.
static std::optional<retro::SaveData> ReadLatestSave(const retro::SaveManager& saves,
                                                     std::optional<int> slot) {
  if (!slot)
    return std::nullopt;
  try {
    return saves.Read(*slot);
  } catch (const std::exception& e) {
    std::cerr << "Carga: " << e.what() << '\n';
  }
  return std::nullopt;
}

// Donde van las fotos (F12): Imagenes/<nombre del juego> del usuario; si no hay carpeta de
// imagenes, junto a las partidas.
static std::filesystem::path PhotosDirectory(const retro::SaveManager& saves, const std::string& game) {
  for (const char* var : {"USERPROFILE", "HOME"}) {
    if (const char* home = std::getenv(var); home && *home) {
      const std::filesystem::path pics = std::filesystem::path(home) / "Pictures";
      if (std::filesystem::is_directory(pics))
        return pics / game;
    }
  }
  return saves.Directory().parent_path() / "fotos";
}

// <id>_AAAA-MM-DD_hh-mm-ss.png en 'dir' (con _2, _3... si ya existe).
static std::filesystem::path NewPhotoPath(const std::filesystem::path& dir, const std::string& id) {
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
  std::filesystem::path path = dir / (id + "_" + stamp + ".png");
  for (int n = 2; std::filesystem::exists(path); ++n)
    path = dir / (id + "_" + stamp + "_" + std::to_string(n) + ".png");
  return path;
}

// La partida guardada mas reciente (de las kSlots), si hay alguna.
static std::optional<int> LatestSave(const retro::SaveManager& saves) {
  std::optional<int> best;
  std::string bestAt;
  for (int slot = 1; slot <= retro::SaveManager::kSlots; ++slot) {
    if (const auto info = saves.Peek(slot); info && (!best || info->savedAt > bestAt)) {
      best = slot;
      bestAt = info->savedAt;
    }
  }
  return best;
}

// Como se ve una partida en la lista: "Partida 2   Puerto Niebla   2:14" (horas:minutos jugados),
// o "Partida 2   (vacia)". 'used' dice si hay partida.
static std::string SlotLabel(const retro::GameConfig& game, const retro::SaveManager& saves, int slot,
                             bool& used) {
  const std::string head = std::string(Tr("Partida")) + " " + std::to_string(slot);
  used = false;
  if (!saves.Exists(slot))
    return head + "   " + Tr("(vacia)");
  try {
    const retro::SaveData d = saves.Read(slot);
    used = true;
    const auto minutes = static_cast<long>(d.playTime / 60.0);
    return head + "   " + Tr(game.PlaceOf(d.scene, d.flags)) + "   " +
           TextFormat("%ld:%02ld", minutes / 60, minutes % 60);
  } catch (const std::exception& e) {
    std::cerr << "Partida " << slot << ": " << e.what() << '\n';
    return head + "   " + Tr("(danada)");
  }
}

// <exe> [--windowed]: la pantalla de inicio. En <exe>Debug, ademas: --scene scenes/x.json,
// directamente jugando en esa escena (para probar); --assets <carpeta>, los assets de otra carpeta
// (su game.json es el de la carpeta de arriba). Para "Probar aqui" del editor: --pos x,y,z y --yaw
// grados (donde aparece), --flags a=1,b=2 (con que banderas) y --prueba (no guarda nada: ni
// autoguardado ni ajustes; las partidas de verdad no se tocan).
int main(int argc, char** argv) {
  std::optional<std::string> sceneArg;
  std::optional<std::string> assetsArg; // --assets: la carpeta de assets (la del editor, al probar)
  std::optional<Vector3> posArg;
  std::optional<float> yawArg;
  std::optional<float> pitchArg; // --pitch: hacia arriba (+) o abajo (-), en grados
  std::vector<std::pair<std::string, int>> flagArgs;
  bool testRun = false;
  bool forceWindowed = false; // --windowed: en ventana aunque los ajustes digan pantalla completa
  for (int i = 1; i < argc; ++i) {
    const std::string a = argv[i];
    const bool more = i + 1 < argc;
    if (a == "--windowed") {
      forceWindowed = true;
      continue;
    }
    if constexpr (!kDebugTools)
      continue; // el resto es de depuracion (probar escenas desde el editor)
    if (a == "--scene" && more)
      sceneArg = argv[++i];
    else if (a == "--prueba")
      testRun = true;
    else if (a == "--assets" && more)
      assetsArg = argv[++i];
    else if (a == "--pos" && more) {
      Vector3 p{};
      if (std::sscanf(argv[++i], "%f,%f,%f", &p.x, &p.y, &p.z) == 3)
        posArg = p;
    } else if (a == "--yaw" && more) {
      yawArg = std::strtof(argv[++i], nullptr);
    } else if (a == "--pitch" && more) {
      pitchArg = std::strtof(argv[++i], nullptr);
    } else if (a == "--flags" && more) {
      std::string list = argv[++i];
      std::size_t start = 0;
      while (start < list.size()) {
        const std::size_t end = std::min(list.find(',', start), list.size());
        const std::string item = list.substr(start, end - start);
        if (const std::size_t eq = item.find('='); eq != std::string::npos && eq > 0)
          flagArgs.emplace_back(item.substr(0, eq), std::atoi(item.c_str() + eq + 1));
        start = end + 1;
      }
    }
  }

  try {
    // El juego: <exe>/<RETRO_GAME_DIR>/ (game.json y assets/), o la carpeta de arriba de --assets.
    namespace fs = std::filesystem;
    retro::EngineConfig config;
    const fs::path exeDir = GetApplicationDirectory();
    if (assetsArg) {
      config.assetRoot = *assetsArg;
      if (!config.assetRoot.empty() && config.assetRoot.back() != '/' && config.assetRoot.back() != '\\')
        config.assetRoot += '/';
    } else {
      config.assetRoot = (exeDir / RETRO_GAME_DIR / "assets").generic_string() + '/';
    }
    const retro::GameConfig game =
        retro::LoadGameConfig(fs::path(config.assetRoot).parent_path().parent_path() / "game.json");
    config.title = game.name;
    config.internalWidth = game.width;
    config.internalHeight = game.height;
    retro::RetroEngine engine(config);
    retro::RetroCamera camera;

    retro::SaveManager saves(retro::SaveManager::DefaultDirectory(game.savesFolder));
    const std::filesystem::path settingsFile = saves.Directory().parent_path() / "ajustes.json";
    retro::Settings settings = retro::LoadSettings(settingsFile);
    if (forceWindowed)
      settings.fullscreen = false; // solo esta vez (no se guarda si no se cambia un ajuste)

    // Sin --scene, siempre la pantalla de inicio: la de la ciudad de la ultima partida (o la
    // aldea de noche, si no hay partida o aun no llego a Ventosa).
    const std::optional<retro::SaveData> titleSave =
        sceneArg ? std::nullopt : ReadLatestSave(saves, LatestSave(saves));
    Screen screen = sceneArg ? Screen::Playing : Screen::Title;
    const TitleSet* title = titleSave ? &game.TitleFor(titleSave->scene, titleSave->flags)
                                      : &game.TitleFor("", {});
    const std::string primera = sceneArg                  ? *sceneArg
                                : screen == Screen::Title ? title->scene
                                                          : game.firstScene;
    retro::SceneManager scenes(engine, camera, {primera});
    scenes.LoadNow(0);
    scenes.FadeInFromBlack();

    retro::Player player;
    retro::GameState state;
    retro::DialogueSystem dialogues(engine.Assets().Root());
    retro::Weather weather(engine);
    // la primera escena ya esta cargada (no pasa por onSceneChanged): su tiempo, desde ya
    weather.Apply(scenes.Current() ? scenes.Current()->weather : std::nullopt);
    weather.ApplyWind(scenes.Current() ? scenes.Current()->wind : std::nullopt);
    retro::ActionRunner runner(scenes, player, state, engine.Audio(), dialogues, weather);

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
    // Ojos cerrados (escenas con "eyes"): 'lids' de 0 (abiertos) a 1 (cerrados), animado;
    // 'drowsy' de 0 a 1 mientras se tienen cerrados. Si gana el sueno, no se vuelven a cerrar
    // hasta soltar Q.
    constexpr const char* kEyesFlag = "ojos_cerrados";
    float lids = 0.0f, drowsy = 0.0f;
    bool eyesForced = false;
    // La camara de fotos (escenas con "photo"): F la levanta y se mira por el visor ('camRaise' va
    // de 0 a 1, animado); clic dispara: el obturador se cierra un instante y el flash. Mientras dura
    // el flash, la bandera "flash" vale 1; 'photoWait' es lo que falta para poder hacer otra.
    constexpr const char* kFlashFlag = "flash";
    constexpr float kFlashSeconds = 0.25f;
    constexpr float kCameraZoom = 14.0f; // grados que se cierra la vista por el visor
    float photoWait = 0.0f, photoAge = 1.0f;
    bool cameraUp = false;
    float camRaise = 0.0f;
    // Lo que se lleva en la mano: la bandera "lleva" (0 = nada) y su nombre en objetos.json.
    const std::unordered_map<int, std::string> itemNames = LoadItemNames(game.itemsFile.empty() ? std::string() : engine.Assets().Resolve(game.itemsFile));
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
    // La partida que se juega (1..kSlots): ahi va el autoguardado y F9. 0 = ninguna (con --scene
    // se juega sin partida: no se guarda nada).
    int activeSlot = 0;
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
    // "Probar aqui" del editor: donde estaba la camara del editor y con sus banderas
    if (posArg) {
      player.Spawn(*posArg, yawArg.value_or(scenes.Current() ? scenes.Current()->spawn.yaw : 0.0f));
      footsteps.Reset(*posArg);
    }
    if (pitchArg)
      player.SetPitch(*pitchArg);
    for (const auto& [name, value] : flagArgs)
      state.Set(name, value);

    auto snapshot = [&]() {
      retro::SaveData d;
      d.scene = scenes.CurrentPath();
      d.position = player.Feet();
      d.yaw = player.Yaw();
      d.pitch = player.Pitch();
      d.playTime = playTime;
      d.flags = state.Flags();
      if (const retro::CylinderCity* c = scenes.City())
        d.city = retro::SaveData::CityPlace{c->Origin(), c->Phi()};
      return d;
    };

    // Guarda la partida activa. 'confirmation': el aviso si sale bien (nullptr = en silencio).
    auto save = [&](const char* confirmation, bool allowDuringTransition = false) {
      if (testRun || activeSlot == 0) { // probando (editor o --scene): no hay partida que tocar
        if (confirmation)
          state.ShowMessage(testRun ? "Prueba del editor: no se guarda" : "Sin partida: no se guarda", 2.0f);
        return;
      }
      const int slot = activeSlot;
      if (scenes.Transitioning() && !allowDuringTransition) {
        if (confirmation)
          state.ShowMessage(Tr("No se puede guardar durante un cambio de escena"), 2.5f);
        return;
      }
      try {
        saves.Write(slot, snapshot());
        if (confirmation)
          state.ShowMessage(confirmation, 2.0f);
      } catch (const std::exception& e) {
        std::cerr << "Guardado: " << e.what() << '\n';
        if (confirmation)
          state.ShowMessage(Tr("Error al guardar (ver consola)"), 3.5f);
      }
    };

    // Carga la partida 'slot', que pasa a ser la activa.
    auto loadFrom = [&](int slot) {
      if (scenes.Transitioning() || pendingLoad)
        return;
      try {
        bool backup = false;
        retro::SaveData data = saves.Read(slot, &backup);
        if (data.city)
          scenes.SetCityStart({data.city->origin, data.city->phi});
        scenes.GoToPath(data.scene, data.position, data.yaw);
        pendingLoad = std::move(data);
        activeSlot = slot;
        if (backup)
          std::cerr << "Carga: el guardado estaba danado; se uso la copia de seguridad\n";
      } catch (const std::exception& e) {
        std::cerr << "Carga: " << e.what() << '\n';
        state.ShowMessage(Tr("No se pudo cargar la partida (ver consola)"), 3.5f);
      }
    };

    // --- Ajustes ---
    ApplySettings(settings, engine, player);
    // El idioma: el de los ajustes si el juego lo tiene; si no, el de origen (el primero).
    auto languageIndex = [&]() -> std::size_t {
      for (std::size_t i = 0; i < game.languages.size(); ++i) {
        if (game.languages[i].code == settings.language)
          return i;
      }
      return 0;
    };
    auto applyLanguage = [&]() {
      const std::size_t i = languageIndex();
      retro::lang::Use(i == 0 ? std::unordered_map<std::string, std::string>{}
                              : retro::lang::LoadTable({engine.Assets().EngineRoot(), engine.Assets().Root()},
                                                       game.languages[i].code));
    };
    applyLanguage();
    auto saveSettings = [&]() {
      if (testRun)
        return;
      try {
        retro::SaveSettings(settingsFile, settings);
      } catch (const std::exception& e) {
        std::cerr << "Ajustes: " << e.what() << '\n';
      }
    };

    // --- Menus: pantalla de inicio, pausa y configuracion ---
    retro::Menu menu;
    menu.SetStyle({game.style.menu, game.style.menuSelected, game.style.menuDisabled, game.style.menuMess});
    Page page = Page::Main;
    // Mientras se va a otra escena desde un menu (jugar, volver al inicio): la pantalla a la que
    // se pasa cuando llegue. El menu no se ve ni responde mientras tanto.
    std::optional<Screen> leaving;
    bool arrival = false; // la pantalla de inicio vino de jugar (title_screen), no del arranque

    auto mainItems = [&]() -> std::vector<retro::Menu::Item> {
      if (screen == Screen::Paused)
        return {{kResume, Tr("Seguir jugando")}, {kSettings, Tr("Configuracion")},
                {kToTitle, Tr("Salir al menu")}, {kQuit, Tr("Salir del juego")}};
      if (arrival) // se llego jugando: se sigue desde aqui
        return {{kPlay, Tr("Jugar")}, {kSettings, Tr("Configuracion")}, {kQuit, Tr("Salir")}};
      const bool any = LatestSave(saves).has_value();
      return {{kContinue, Tr("Continuar"), "", any}, {kNewGame, Tr("Nueva partida")},
              {kLoadGame, Tr("Cargar partida"), "", any}, {kSettings, Tr("Configuracion")}, {kQuit, Tr("Salir")}};
    };
    // La lista de partidas: para empezar una nueva (todas se pueden elegir; si ya hay una, se
    // pregunta) o para cargar (solo las que hay).
    bool slotsForNew = true;
    int confirmSlot = 0; // la partida que se borraria al empezar
    auto slotItems = [&]() -> std::vector<retro::Menu::Item> {
      std::vector<retro::Menu::Item> items;
      for (int n = 1; n <= retro::SaveManager::kSlots; ++n) {
        bool used = false;
        std::string label = SlotLabel(game, saves, n, used);
        items.emplace_back(kSlot0 + n, std::move(label), "", slotsForNew || used);
      }
      items.emplace_back(kBack, Tr("Volver"));
      return items;
    };
    auto settingsItems = [&]() -> std::vector<retro::Menu::Item> {
      static constexpr const char* kFilters[] = {"Pixeles", "Nitida", "Suave"};
      settings.fullscreen = IsWindowState(FLAG_BORDERLESS_WINDOWED_MODE); // tambien con F11
      std::vector<retro::Menu::Item> items = {
          {kVolume, Tr("Volumen"), TextFormat("%d%%", static_cast<int>(std::lround(settings.volume * 100.0f)))},
          {kSensitivity, Tr("Raton"), std::to_string(settings.sensitivity)},
          {kFullscreen, Tr("Pantalla completa"), settings.fullscreen ? Tr("Si") : Tr("No")},
          {kFilter, Tr("Imagen"), Tr(kFilters[static_cast<int>(settings.filter)])},
          {kEffects, Tr("Grano y vineta"), settings.effects ? Tr("Si") : Tr("No")}};
      if (game.languages.size() > 1) // el nombre de cada idioma, en su idioma
        items.emplace_back(kLanguage, Tr("Idioma"), game.languages[languageIndex()].name);
      items.emplace_back(kBack, Tr("Volver"));
      return items;
    };
    auto openPage = [&](Page p) {
      page = p;
      switch (p) {
      case Page::Main:
        menu.SetItems(mainItems());
        break;
      case Page::Settings:
        menu.SetItems(settingsItems());
        break;
      case Page::Slots:
        menu.SetItems(slotItems());
        break;
      case Page::Confirm:
        menu.SetItems({{kConfirm, Tr("Si, borrarla")}, {kBack, Tr("No")}});
        break;
      }
      menu.Select(p == Page::Confirm ? 1 : 0); // al borrar, lo seguro va primero
    };
    // La intro: por que toma va y cuanto lleva (sin contar los fundidos).
    std::size_t introShot = 0;
    float introTime = 0.0f;
    // Empieza una partida nueva en la ranura 'slot' (lo que hubiera ahi se borra): primero la intro.
    auto startNewGame = [&](int slot) {
      if (!testRun)
        saves.Delete(slot);
      activeSlot = slot;
      state.Clear();
      playTime = 0.0;
      introShot = 0;
      introTime = 0.0f;
      camera.SetUp({0.0f, 1.0f, 0.0f}); // la intro y la partida, derechas
      if (game.intro.empty()) { // sin intro: directo a la primera escena
        scenes.GoToPath(game.firstScene);
        leaving = Screen::Playing;
        return;
      }
      scenes.SetMusicOverride(game.introMusic, game.introVolume);
      scenes.GoToPath(game.intro[0].scene);
      leaving = Screen::Intro;
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
      case kLanguage: {
        const std::size_t n = game.languages.size();
        settings.language = game.languages[(languageIndex() + n + static_cast<std::size_t>(dir)) % n].code;
        applyLanguage();
        break;
      }
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
    float titleMess = 0.85f;   // lo roto que se ve el titulo (antes de Ventosa; menos en cada vuelta)
    bool titleMusic = false;   // ya suena el vals con percusion
    struct Glide {
      Vector3 fromEye;
      Vector3 fromLook;
      Vector3 fromUp; // si la pantalla de inicio giraba, se endereza por el camino
      float t = 0.0f;
    };
    bool spinning = false; // la pantalla de inicio gira alrededor de un eje ("spin")
    float spinAngle = 0.0f;
    std::optional<Glide> glide;
    auto startTitle = [&]() {
      title = &game.TitleFor(scenes.CurrentPath(), state.Flags());
      // al arrancar todavia no hay banderas: las de la partida que da la pantalla de inicio
      titleMess = game.TitleMess(state.Flags().empty() && titleSave ? titleSave->flags : state.Flags());
      spinning = title->spin && retro::GameConfig::Here(*title, scenes.CurrentPath());
      spinAngle = 0.0f;
      camera.SetUp({0.0f, 1.0f, 0.0f});
      if (OrbitsHere(*title, scenes.CurrentPath())) {
        orbitCenter = title->center;
        orbitRadius = title->radius;
        orbitHeight = title->height;
        orbitAngle = 0.0f;
      } else {
        orbitCenter = camera.Target();
        const Vector3 d = Vector3Subtract(camera.Position(), orbitCenter);
        orbitRadius = std::max(std::hypot(d.x, d.z), 4.0f);
        orbitHeight = d.y;
        orbitAngle = std::atan2(d.x, d.z);
      }
      titleMusic = false; // cambia cuando el menu aparezca (la de la escena suena en el negro)
      menuAge = 0.0f;
      player.SetActive(false);
      openPage(Page::Main);
    };
    auto orbit = [&](float dt) {
      if (!scenes.Transitioning()) {
        menuAge += dt;
        if (!titleMusic) {
          engine.Audio().PlayMusic(title->music, 0.55f, 1.5f); // en lugar de la de la escena
          titleMusic = true;
        }
      }
      if (glide) { // "Jugar": de la vuelta al tiovivo a los ojos del jugador
        glide->t = std::min(glide->t + dt / 3.0f, 1.0f);
        const float k = glide->t * glide->t * (3.0f - 2.0f * glide->t);
        const Vector3 eye = player.Eye();
        const float yaw = player.Yaw() * DEG2RAD;
        const Vector3 look = Vector3Add(eye, {-std::sin(yaw), 0.0f, -std::cos(yaw)});
        camera.SetUp(Vector3Normalize(Vector3Lerp(glide->fromUp, {0.0f, 1.0f, 0.0f}, k)));
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
      if (spinning) { // el ojo, a donde mira y su "arriba", girando juntos alrededor del eje
        spinAngle += dt * title->spinSpeed * DEG2RAD;
        const Quaternion q = QuaternionFromAxisAngle(Vector3Normalize(title->spinAxis), spinAngle);
        auto rot = [&](Vector3 p) {
          return Vector3Add(title->spinCenter, Vector3RotateByQuaternion(Vector3Subtract(p, title->spinCenter), q));
        };
        const Vector3 eye = rot(title->spinEye);
        camera.SetUp(Vector3RotateByQuaternion({0.0f, 1.0f, 0.0f}, q));
        camera.SetPosition(eye);
        camera.LookAt(rot(title->spinLook));
        scenes.SetFocus(eye);
        return;
      }
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
        if (page == Page::Confirm)
          openPage(Page::Slots);
        else if (page != Page::Main)
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
          glide = Glide{camera.Position(), camera.Target(), camera.Up()};
          arrival = false;
          if (const retro::Scene* s = scenes.Current())
            scenes.PlaySceneMusic(s->audio, 2.5f);
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
        case kLoadGame:
          slotsForNew = r.id == kNewGame;
          openPage(Page::Slots);
          break;
        case kConfirm:
          startNewGame(confirmSlot);
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
          save(nullptr);
          state.ClearMessage();
          dialogueFocus.reset();
          scenes.GoToPath(game.TitleFor(scenes.CurrentPath(), state.Flags()).scene);
          leaving = Screen::Title;
          break;
        case kBack:
          openPage(page == Page::Confirm ? Page::Slots : Page::Main);
          break;
        default:
          if (r.id > kSlot0 && r.id <= kSlot0 + retro::SaveManager::kSlots) {
            const int slot = r.id - kSlot0;
            if (!slotsForNew) { // cargar
              loadFrom(slot);
              if (pendingLoad)
                leaving = Screen::Playing;
            } else if (saves.Exists(slot)) { // ya hay una: se pregunta antes de borrarla
              confirmSlot = slot;
              openPage(Page::Confirm);
            } else {
              startNewGame(slot);
            }
          }
          break;
        }
        break;
      }
    };

    // Al llegar a una escena: si se venia de un menu, se cambia de pantalla; en la de inicio la
    // camara empieza a dar vueltas; jugando, se coloca al jugador (y se aplica la partida cargada
    // o se autoguarda).
    auto applyFloor = [&]() {
      const retro::Scene* s = scenes.Current();
      if (s && s->floor)
        player.SetBaseFloor(*s->floor);
      else if (s)
        player.NoBaseFloor();
    };
    applyFloor(); // la primera escena no pasa por onSceneChanged
    auto onSceneChanged = [&]() {
      applyFloor();
      zoom = 0.0f; // la escena nueva ya puso su campo de vision
      lids = drowsy = 0.0f;
      eyesForced = false;
      state.Set(kEyesFlag, 0);
      runner.CancelPending();
      photoWait = 0.0f;
      photoAge = 1.0f;
      cameraUp = false;
      camRaise = 0.0f;
      state.Set(kFlashFlag, 0);
      weather.Apply(scenes.Current() ? scenes.Current()->weather : std::nullopt);
      weather.ApplyWind(scenes.Current() ? scenes.Current()->wind : std::nullopt);
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
      if (screen == Screen::Intro) {
        introTime = 0.0f;
        player.SetActive(false);
        const IntroShot& shot = game.intro[introShot];
        camera.SetPosition(shot.eyeFrom);
        camera.LookAt(shot.lookFrom);
        return;
      }
      if (screen == Screen::Title) {
        startTitle();
        if (arrival) { // se llego jugando: el jugador ya esta aqui (y se guarda aqui)
          spawnPlayer();
          save(nullptr, true);
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
        state.ShowMessage(Tr("Partida cargada"), 2.0f);
      } else {
        save(nullptr, true);
      }
    };

    if (screen == Screen::Title) {
      startTitle();
    } else {
      player.SetActive(true);
      if constexpr (kDebugTools) // solo se llega aqui con --scene
        state.ShowMessage("Jugando sin partida (--scene): no se guarda nada.", 4.0f);
    }

    engine.Run(
        [&](float dt) {
          // --- Fotos (F12): el motor la saca al final del fotograma; el aviso, en el siguiente ---
          if (const auto saved = engine.TakeScreenshotResult())
            state.ShowMessage(*saved ? Tr("Foto guardada") : Tr("No se pudo guardar la foto (ver consola)"),
                              2.0f);
          if (IsKeyPressed(KEY_F12)) {
            try {
              const std::filesystem::path dir = PhotosDirectory(saves, game.name);
              std::filesystem::create_directories(dir);
              const std::filesystem::path file = NewPhotoPath(dir, game.id);
              std::cout << "Foto: " << file.string() << std::endl;
              engine.RequestScreenshot(file.string());
            } catch (const std::exception& e) {
              std::cerr << "Foto: " << e.what() << '\n';
              state.ShowMessage(Tr("No se pudo guardar la foto (ver consola)"), 2.5f);
            }
          }

          // --- Menus (inicio y pausa) ---
          if ((screen == Screen::Title || screen == Screen::Paused) && !leaving && !glide &&
              !scenes.Transitioning() && menuAge > 0.4f)
            runMenu();
          else if (screen == Screen::Playing && IsKeyPressed(KEY_ESCAPE) && !scenes.Transitioning())
            pause();
          if (screen == Screen::Paused && !leaving)
            return; // en pausa el mundo se queda quieto
          if (screen != Screen::Playing) {
            // la pantalla de inicio de fondo, o el fundido al salir de un menu
            if (screen == Screen::Title)
              orbit(dt);
            if (screen == Screen::Intro && !leaving) {
              // la camara se desliza despacio; al acabar la toma (o al saltarla), la siguiente o el tren
              if (!scenes.Transitioning())
                introTime += dt;
              const IntroShot& shot = game.intro[introShot];
              const float u = std::clamp(introTime / shot.seconds, 0.0f, 1.0f);
              const float k = u * u * (3.0f - 2.0f * u);
              camera.SetPosition(Vector3Lerp(shot.eyeFrom, shot.eyeTo, k));
              camera.LookAt(Vector3Lerp(shot.lookFrom, shot.lookTo, k));
              const bool saltar = introTime > 0.6f && (IsKeyPressed(KEY_E) || IsKeyPressed(KEY_ENTER) ||
                                                       IsKeyPressed(KEY_SPACE) || IsKeyPressed(KEY_ESCAPE));
              if (!scenes.Transitioning() && (saltar || introTime >= shot.seconds)) {
                if (saltar || introShot + 1 >= game.intro.size()) {
                  scenes.SetMusicOverride("", 0.0f);
                  scenes.GoToPath(game.firstScene);
                  leaving = Screen::Playing;
                } else if (game.intro[introShot + 1].scene == game.intro[introShot].scene) {
                  ++introShot;                                   // la misma escena: la camara sigue
                  introTime = 0.0f;
                } else {
                  ++introShot;
                  scenes.GoToPath(game.intro[introShot].scene);
                  leaving = Screen::Intro;
                }
              }
            }
            scenes.Update(dt);
            scenes.TakeOriginMove(); // sin jugador (inicio, intro): la camara no se lleva
            if (scenes.Generation() != seenGeneration) {
              seenGeneration = scenes.Generation();
              onSceneChanged();
            } else if (leaving && !scenes.Transitioning()) {
              leaving.reset(); // no se pudo cambiar de escena: el menu vuelve
              if (std::exchange(pendingLoad, std::nullopt))
                state.ShowMessage(Tr("No se pudo cargar la escena guardada (ver consola)"), 3.5f);
            }
            if (retro::Scene* s = scenes.CurrentMut())
              s->RefreshVisibility(state);
            scenes.UpdateAudio(dt);
            return;
          }

          playTime += dt;

          if constexpr (kDebugTools) { // solo en RetroEngineDebug.exe
            if (IsKeyPressed(KEY_F9)) // guardar ya la partida activa
              save(Tr("Partida guardada"));

            if (IsKeyPressed(KEY_F10) && activeSlot != 0) // volver a lo ultimo guardado
              loadFrom(activeSlot);

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
          }

          scenes.SetFocus(player.Feet(), true);
          scenes.Update(dt);
          // la ciudad sin fin recentro el mundo (en Z, o girando alrededor del eje): el jugador va con
          // el, sin perder el paso (no se nota)
          if (const auto move = scenes.TakeOriginMove(); move.dz != 0.0f || move.turn != 0.0f) {
            Vector3 f = player.Feet();
            const float a = move.turn * DEG2RAD;
            f = {f.x * std::cos(a) - f.y * std::sin(a), f.x * std::sin(a) + f.y * std::cos(a), f.z - move.dz};
            player.CarryFeet(f);
          }
          state.Update(dt);
          letterSounds.Update(state, engine.Audio());

          if (scenes.Generation() != seenGeneration) {
            seenGeneration = scenes.Generation();
            onSceneChanged();
          } else if (pendingLoad && !scenes.Transitioning()) {
            pendingLoad.reset();
            state.ShowMessage(Tr("No se pudo cargar la escena guardada (ver consola)"), 3.5f);
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
              if (const retro::Scene* sc = scenes.Current())
                player.SetPush(weather.WindPush(player.Feet(), *sc, state, dt));
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

          // con sigilo (Ctrl) no se oyen los pasos
          const bool sneaking = IsKeyDown(KEY_LEFT_CONTROL) || IsKeyDown(KEY_RIGHT_CONTROL);
          footsteps.Update(player.Feet(), player.Grounded(), IsKeyDown(KEY_LEFT_SHIFT),
                           firstPerson && !scenes.Transitioning() && !sneaking, engine.Audio());
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
                state.ShowMessage(TextFormat(Tr("Tiempo! %d aciertos."), gallery->score), 3.0f);
              gallery.reset();
            }
          }

          // --- Ojos cerrados (Q) ---
          {
            const retro::Scene* s = scenes.Current();
            // Un susto (scare) visto con los ojos cerrados se ve con los ojos cerrados: mientras
            // dura, siguen asi aunque se suelte Q (si no, lo que solo esta con los ojos cerrados
            // desapareceria justo cuando la vista se le acerca). Y el sueno no avanza.
            const bool scaring = dialogueFocus && dialogueFocus->fov > 0.0f;
            const bool allowed = s && s->eyes && screen == Screen::Playing && firstPerson &&
                                 !dialogues.Active() && !scenes.Transitioning() && !riding &&
                                 !gallery;
            if (!IsKeyDown(KEY_Q))
              eyesForced = false;
            const bool holdShut = scaring && lids > 0.5f;
            const bool closed = allowed && (IsKeyDown(KEY_Q) || holdShut) && !eyesForced;
            const float before = drowsy;
            if (closed && !scaring) {
              drowsy = std::min(1.0f, drowsy + dt / s->eyes->seconds);
              if (before < 0.6f && drowsy >= 0.6f)
                engine.Audio().PlaySfx("audio/latido.wav", 0.7f);
              if (drowsy >= 1.0f) {
                eyesForced = true; // gana el sueno: los ojos se abren de golpe
                drowsy = 0.4f;     // y el susto despierta: la proxima vez cuesta otra vez
                runner.Run(s->eyes->onSleep);
              }
            } else if (closed) {
              // en pleno susto: ni sube ni baja
            } else {
              drowsy = std::max(0.0f, drowsy - dt * 0.3f);
            }
            const bool shut = closed && !eyesForced;
            state.Set(kEyesFlag, shut ? 1 : 0);
            const float target = shut ? 1.0f : 0.0f;
            lids += std::clamp(target - lids, -dt * 4.0f, dt * 4.0f);
            retro::PostParams& post = engine.Post().Params();
            post.eyes = lids;
            post.drowsy = shut ? drowsy : drowsy * 0.5f;
          }

          // --- La camara de fotos: F la levanta o la baja, clic dispara ---
          {
            const retro::Scene* s = scenes.Current();
            photoWait = std::max(0.0f, photoWait - dt);
            photoAge += dt;
            const bool allowed = s && s->photo && screen == Screen::Playing && firstPerson &&
                                 !dialogues.Active() && !scenes.Transitioning() && !gallery &&
                                 !dialogueFocus && retro::AllHold(s->photo->conditions, state);
            if (!allowed)
              cameraUp = false;
            else if (IsKeyPressed(KEY_F))
              cameraUp = !cameraUp;
            camRaise = std::clamp(camRaise + (cameraUp ? dt : -dt) * 5.0f, 0.0f, 1.0f);
            if (cameraUp && camRaise >= 1.0f && photoWait <= 0.0f && IsMouseButtonPressed(MOUSE_BUTTON_LEFT)) {
              photoWait = s->photo->cooldown;
              photoAge = 0.0f;
              weather.PhotoFlash();
              engine.Audio().PlaySfx("audio/flash.wav", 0.8f);
              state.Set("fotos", state.Get("fotos") + 1);
            }
            state.Set(kFlashFlag, photoAge < kFlashSeconds ? 1 : 0);
          }

          runner.Update(dt);
          // Lo rapido que se va (decimetros por segundo): lo que caza por el oido lo usa.
          state.Set("velocidad", screen == Screen::Playing && firstPerson && !riding
                                     ? static_cast<int>(player.HorizontalSpeed() * 10.0f + 0.5f)
                                     : 0);
          weather.Update(dt, state);
          if (retro::Scene* scene = scenes.CurrentMut())
            scene->weatherHaze = weather.Haze();
          engine.Post().Params().flash = weather.Flash();
          // El flash de la camara alumbra desde la camara: blanco frio, fuerte cerca y nada lejos.
          if (const float f = weather.Photo(); f > 0.01f) {
            const Vector3 eye = camera.Position();
            const Vector3 ahead = Vector3Normalize(Vector3Subtract(camera.Target(), eye));
            retro::PointLight luz;
            luz.position = Vector3Add(eye, Vector3Scale(ahead, 0.6f));
            luz.color = Vector3Scale(Vector3{1.0f, 1.04f, 1.12f}, 3.2f * f);
            luz.radius = 26.0f;
            scenes.SetFlashLight(luz);
          } else {
            scenes.SetFlashLight(std::nullopt);
          }
          // El farolillo en la mano ("lantern" de la escena): delante y un poco abajo de los ojos.
          {
            std::optional<retro::PointLight> hand;
            const retro::Scene* s = scenes.Current();
            if (s && s->lantern && screen == Screen::Playing && retro::AllHold(s->lantern->conditions, state)) {
              const float yaw = player.Yaw() * DEG2RAD;
              const Vector3 ahead{-std::sin(yaw), 0.0f, -std::cos(yaw)};
              const Vector3 right{std::cos(yaw), 0.0f, -std::sin(yaw)};
              const Vector3& o = s->lantern->offset;
              retro::PointLight l = s->lantern->light;
              l.position = Vector3Add(player.Eye(), Vector3Add(Vector3Scale(right, o.x),
                                                               Vector3Add({0.0f, o.y, 0.0f}, Vector3Scale(ahead, o.z))));
              const float f = retro::FlickerFactor(l, GetTime());
              l.color = Vector3Scale(l.color, f);
              hand = l;
            }
            scenes.SetHandLight(hand);
          }

          // --- Triggers ---
          if (firstPerson && !scenes.Transitioning() && !riding) {
            if (retro::Scene* s = scenes.CurrentMut()) {
              const retro::PlayerSettings& cfg = player.Settings();
              firedActions.clear();
              s->UpdateTriggers(player.Feet(), cfg.radius, cfg.height, state, firedActions);
              // lo que se ve: entidades con "seen" en el centro de la vista
              const Vector3 eye = camera.Position();
              s->UpdateSight(eye, Vector3Normalize(Vector3Subtract(camera.Target(), eye)), dt, state,
                             firedActions);
              s->UpdateNear(player.Feet(), dt, firedActions); // lo que esta cerca (lo que se oye)
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
                interactPrompt = "E: " + Tr(hit.entity->interact->prompt);
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
            // (por el visor de la camara tambien se cierra un poco: es otra lente)
            const float want =
                dialogueFocus && dialogueFocus->fov > 0.0f
                    ? std::max(0.0f, sceneFov - dialogueFocus->fov)
                    : kCameraZoom * camRaise;
            const float rate = dialogueFocus ? dialogueFocus->rate : 10.0f;
            zoom += (want - zoom) * std::min(1.0f, dt * rate);
            camera.SetFov(sceneFov - zoom);
          }

          if (retro::Scene* s = scenes.CurrentMut())
            s->RefreshVisibility(state);

          if constexpr (kDebugTools)
            HandleDebugKeys(engine);
        },

        [&]() {
          camera.Begin();
          scenes.Draw();
          weather.Draw(camera.Position()); // lineas con el shader de Raylib: sin el ajuste de
                                           // profundidad de los nuestros, que en lineas no vale

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

          if constexpr (kDebugTools) { // la ayuda de depuracion (F1)
            if (showHelp) {
              ui.Text("1 jitter 2 color 3 dither 4 niebla 6 luces", 4, 3, 8, RAYWHITE);
              ui.Text("F5 recargar  F6 reset  F7 filtro  F8 acabado", 4, 13, 8, RAYWHITE);
              ui.Text(firstPerson ? "WASD mover  Shift correr  Ctrl despacio  Espacio saltar  E usar"
                                  : "Arrastrar/flechas: orbitar  Rueda: zoom",
                      4, 23, 8, RAYWHITE);
              ui.Text("Tab escena  F1 ayuda  F2 camara  F3 colisiones", 4, 33, 8, RAYWHITE);
              ui.Text("F9 guardar  F10 volver a lo guardado  M silencio", 4, 43, 8, RAYWHITE);
              ui.Text(TextFormat("Escena %d/%d: %s", static_cast<int>(scenes.Index()) + 1,
                                 static_cast<int>(scenes.Count()), scenes.CurrentPath().c_str()),
                      4, h - 12.0f, 8.0f, RAYWHITE);
              const Vector3 f = player.Feet();
              const std::string donde = scenes.City()
                                            ? TextFormat("%.1f, %.1f, %.1f   sector %lld   %.2f grados%s", f.x, f.y, f.z,
                                                         scenes.City()->SectorAt(f.z), scenes.City()->Phi(),
                                                         scenes.City()->Indoors() ? "   dentro" : "")
                                            : TextFormat("%.1f, %.1f, %.1f", f.x, f.y, f.z);
              ui.Text(donde, 4, h - 22.0f, 8.0f, RAYWHITE);
            }
          }

          // El visor de la camara: entra desde abajo al levantarla. Fuera del cuadro, negro; dentro,
          // las esquinas, la cruz del centro y lo que cuenta la camara. Al disparar, el obturador se
          // cierra un instante (antes de que el flash lo blanquee todo).
          if (camRaise > 0.0f && screen == Screen::Playing) {
            const float k = camRaise * camRaise * (3.0f - 2.0f * camRaise);
            const float drop = (1.0f - k) * h;
            const float mx = w * 0.07f, my = h * 0.09f;
            const Color negro{4, 4, 6, static_cast<unsigned char>(255 * k)};
            ui.Rect(0, drop, w, my, negro);
            ui.Rect(0, drop + h - my, w, my, negro);
            ui.Rect(0, drop + my, mx, h - 2 * my, negro);
            ui.Rect(w - mx, drop + my, mx, h - 2 * my, negro);
            const Color linea{226, 222, 210, static_cast<unsigned char>(200 * k)};
            const float x0 = mx + 6, y0 = drop + my + 6, x1 = w - mx - 6, y1 = drop + h - my - 6, l = 14.0f;
            for (const auto& [cx, cy, sx, sy] : {std::tuple{x0, y0, 1.f, 1.f}, std::tuple{x1, y0, -1.f, 1.f},
                                                  std::tuple{x0, y1, 1.f, -1.f}, std::tuple{x1, y1, -1.f, -1.f}}) {
              ui.Rect(sx > 0 ? cx : cx - l, cy, l, 1, linea);
              ui.Rect(cx, sy > 0 ? cy : cy - l, 1, l, linea);
            }
            const float cx = w * 0.5f, cy = drop + h * 0.5f;
            ui.Rect(cx - 6, cy, 4, 1, linea);
            ui.Rect(cx + 3, cy, 4, 1, linea);
            ui.Rect(cx, cy - 6, 1, 4, linea);
            ui.Rect(cx, cy + 3, 1, 4, linea);
            ui.RectLines(cx - 22, cy - 16, 44, 32, Color{226, 222, 210, static_cast<unsigned char>(90 * k)});
            const std::string cuenta = TextFormat(Tr("FOTO %02d"), state.Get("fotos"));
            ui.Text(cuenta, x1 - ui.TextWidth(cuenta, 7.0f), y1 - 9, 7.0f, linea);
            ui.Text(photoWait > 0.0f ? Tr("cargando...") : Tr("clic: foto   F: bajar"), x0, y1 - 9, 7.0f, linea);
            if (photoWait <= 0.0f && static_cast<int>(GetTime() * 2.0) % 2 == 0)
              ui.Rect(x0 + 2, y0 + 2, 3, 3, Color{220, 40, 36, static_cast<unsigned char>(230 * k)});
            if (photoAge < 0.07f)                                     // el obturador
              ui.Rect(0, 0, w, h, Color{0, 0, 0, 255});
          } else if (const retro::Scene* s = scenes.Current(); screen == Screen::Playing && !showHelp && s &&
                                                                 s->photo && retro::AllHold(s->photo->conditions, state)) {
            const char* hint = Tr("F: camara");
            ui.Text(hint, w - ui.TextWidth(hint, 8.0f) - 6.0f, h - 14.0f, 8.0f, Color{230, 214, 180, 200});
          }
          if (const int item = state.Get("lleva"); screen == Screen::Playing && item > 0 && !showHelp) {
            const auto it = itemNames.find(item);
            const std::string text = Tr("Llevas: ") + (it != itemNames.end() ? Tr(it->second) : std::to_string(item));
            ui.Text(text, 6.0f, h - 14.0f, 8.0f, Color{230, 214, 180, 230});
          }
          if (screen == Screen::Playing && riding && !riding->locked) {
            const char* hint = Tr("E: bajar");
            ui.Text(hint, (w - ui.TextWidth(hint, 9.0f)) * 0.5f, h - 16.0f, 9.0f,
                    Color{230, 214, 180, 220});
          }
          if (screen == Screen::Playing && gallery) {
            const std::string score = TextFormat(Tr("ACIERTOS %d    %02d"), gallery->score,
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
          if (screen == Screen::Intro && !leaving) {
            // las frases: aparecen despacio, se quedan y se van antes del fundido
            const IntroShot& shot = game.intro[introShot];
            const float a = std::clamp(std::min(introTime - 0.6f, shot.seconds - 0.4f - introTime) / 1.3f, 0.0f, 1.0f);
            if (a > 0.0f) {
              std::string texto = Tr(shot.text);
              std::vector<std::string> lineas;
              for (std::size_t i = 0, j; i <= texto.size(); i = j + 1) {
                j = texto.find('\n', i);
                if (j == std::string::npos)
                  j = texto.size();
                lineas.push_back(texto.substr(i, j - i));
              }
              const float alto = 13.0f;
              const float y0 = h * 0.68f - alto * static_cast<float>(lineas.size()) * 0.5f;
              for (std::size_t k = 0; k < lineas.size(); ++k) {
                const float tw = ui.TextWidth(lineas[k], 10.0f);
                ui.Text(lineas[k], (w - tw) * 0.5f, y0 + alto * static_cast<float>(k), 10.0f,
                        {236, 222, 196, static_cast<unsigned char>(255.0f * a)});
              }
            }
            const char* saltar = Tr("E o Esc: saltar");
            ui.Text(saltar, w - ui.TextWidth(saltar, 7.0f) - 6.0f, h - 12.0f, 7.0f, {150, 140, 130, 150});
            return;
          }
          if (screen == Screen::Playing || leaving || glide || scenes.Transitioning())
            return;
          // aparece despacio (la primera vez, al salir del negro, el titulo se descubre)
          const float a = std::clamp(menuAge / 1.5f, 0.0f, 1.0f);
          auto fade = [a](Color c) {
            c.a = static_cast<unsigned char>(static_cast<float>(c.a) * a);
            return c;
          };
          const Color cream = fade(game.style.title);
          const Color dim = fade(game.style.subtitle);
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
                      fade({game.style.veil.r, game.style.veil.g, game.style.veil.b,
                            static_cast<unsigned char>(std::min(veil, 150.0f) * game.style.veil.a / 255.0f)}));
            }
            const std::string& sub = Tr(game.subtitle);
            const std::string& titulo = Tr(game.title);
            if (title->distorted) { // el titulo como una senal que no sintoniza: casi no se lee
              retro::DrawGarbled(ui, titulo, w * 0.5f, 28.0f, 22.0f, cream, GetTime(), 11u, titleMess);
              retro::DrawGarbled(ui, sub, w * 0.5f, 54.0f, 8.0f, dim, GetTime() + 0.37, 29u, std::min(1.0f, titleMess * 1.2f));
            } else {
              const float tw = ui.TextWidth(titulo, 22.0f);
              ui.Text(titulo, (w - tw) * 0.5f, 28.0f, 22.0f, cream);
              ui.Text(sub, (w - ui.TextWidth(sub, 8.0f)) * 0.5f, 54.0f, 8.0f, dim);
            }
          } else {
            ui.Rect(0.0f, 0.0f, w, h, Color{0, 0, 0, 150});
            const char* pausa = Tr("PAUSA");
            const float tw = ui.TextWidth(pausa, 18.0f);
            ui.Text(pausa, (w - tw) * 0.5f, 50.0f, 18.0f, cream);
          }
          // una lista larga (la configuracion) sube lo que haga falta para no pisar la ayuda de abajo
          top = std::min(top, h - 20.0f - menu.Height() - (page != Page::Main ? 4.0f : 0.0f));
          if (page != Page::Main) { // la cabecera de cada pagina
            const std::string head =
                page == Page::Settings ? std::string(Tr("Configuracion"))
                : page == Page::Confirm
                    ? std::string(TextFormat(Tr("Se borrara la Partida %d. Empezar de nuevo?"), confirmSlot))
                : slotsForNew ? std::string(Tr("Nueva partida: elige donde"))
                              : std::string(Tr("Cargar partida"));
            ui.Text(head, (w - ui.TextWidth(head, 9.0f)) * 0.5f, top - 14.0f, 9.0f, dim);
            top += 4.0f;
          }
          menu.Draw(ui, top, a);
          const char* hint = page == Page::Settings ? Tr("W/S elegir   A/D cambiar   Esc volver")
                             : page == Page::Main   ? Tr("W/S elegir   E o Enter aceptar")
                                                    : Tr("W/S elegir   E o Enter aceptar   Esc volver");
          ui.Text(hint, (w - ui.TextWidth(hint, 7.0f)) * 0.5f, h - 14.0f, 7.0f,
                  fade(game.style.hint));
        });

    if (screen != Screen::Title &&
        (scenes.Generation() != initialGeneration || state.Flags() != initialFlags)) {
      save(nullptr, true);
    }
  } catch (const std::exception& e) {
    std::cerr << "Error fatal: " << e.what() << '\n';
    return 1;
  }
  return 0;
}