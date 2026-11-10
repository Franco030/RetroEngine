#pragma once

#include <raylib.h>

#include <filesystem>
#include <string>
#include <unordered_map>
#include <vector>

namespace retro {

// Lo que hace de un juego ese juego y no otro: el game.json de su carpeta (games/<id>/game.json). El
// motor (el bucle, los menus, las partidas, la intro, la pantalla de inicio) es el mismo para todos;
// aqui van el nombre, el titulo, por donde empieza, sus pantallas de inicio y su intro.
struct GameConfig {
  // Una pantalla de inicio: la escena que se ve de fondo (con su musica de menu) y cuando toca. Toca
  // si la escena de la partida empieza por uno de 'prefixes' o si esta puesta una de 'flags'; se
  // mira en orden y gana la primera (la ultima, sin condiciones, es la de siempre).
  struct TitleScreen {
    std::string scene;
    std::vector<std::string> prefixes;
    std::vector<std::string> flags;
    std::string music;
    bool fixedOrbit = false;    // la camara da vueltas a un punto fijo (si no, al "target" de la escena)
    Vector3 center{0.0f, 0.0f, 0.0f};
    float radius = 12.0f;
    float height = 3.0f;
    // "spin": la camara gira alrededor de un eje (el ojo, a donde mira y su "arriba", juntos):
    // parece que el mundo gira (el cilindro de Onus Somaticum). Gana a "orbit".
    bool spin = false;
    Vector3 spinCenter{0.0f, 0.0f, 0.0f};
    Vector3 spinAxis{0.0f, 0.0f, 1.0f};
    Vector3 spinEye{0.0f, 0.0f, 0.0f};
    Vector3 spinLook{0.0f, 0.0f, 10.0f};
    float spinSpeed = 4.0f;     // grados por segundo
    bool distorted = false;     // el titulo como una senal que no sintoniza
    std::string place;          // como se llama en la lista de partidas
  };

  // Un sitio con nombre propio en la lista de partidas, aunque no tenga pantalla de inicio.
  struct Place {
    std::string prefix;
    std::string name;
  };

  // Una toma de la intro de la partida nueva: la escena, de donde a donde va la camara y hacia donde
  // mira, cuanto dura y su texto. Si la siguiente es de la misma escena, la camara sigue sin cortar.
  struct IntroShot {
    std::string scene;
    Vector3 eyeFrom{}, eyeTo{}, lookFrom{}, lookTo{};
    float seconds = 8.0f;
    std::string text;
  };

  std::string id;           // la carpeta del juego (games/<id>); tambien el nombre de las fotos
  std::string name = "RetroEngine";  // la ventana y la carpeta de fotos
  std::string exe = "RetroGame";     // el ejecutable (<exe>.exe y <exe>Debug.exe)
  std::string savesFolder = "RetroEngine"; // %APPDATA%/<savesFolder>/saves (y los ajustes)
  std::string title;        // el titulo grande de la pantalla de inicio
  std::string subtitle;
  std::string firstScene;   // donde empieza una partida nueva
  // La imagen interna (lo que se dibuja antes de escalar a la ventana): 320x240 es la PS1; un juego
  // de escala enorme (Onus Somaticum) la sube para que no se lo coman los pixeles. La interfaz
  // no cambia: va en su rejilla de 320 de ancho.
  int width = 320;
  int height = 240;
  std::string itemsFile;    // nombres de lo que se lleva en la mano ("lleva"): { "1": "Una carta", ... }

  std::vector<TitleScreen> titleScreens;
  std::vector<Place> places;

  // Lo roto que se ve un titulo distorsionado: base x factor^(valor de 'flag'), p. ej. una vuelta del
  // ciclo mas, menos roto.
  std::string distortionFlag;
  float distortionBase = 0.85f;
  float distortionFactor = 0.6f;

  // Los idiomas en los que se puede jugar ("languages": [{ "code", "name" }]). El primero es el de
  // origen (en el que estan escritos los textos); los demas tienen su lang/<code>.json.
  struct Language {
    std::string code;
    std::string name;
  };
  std::vector<Language> languages;

  // Los colores del titulo y los menus, y lo roto que se ve el menu (0 = limpio). Lo de por
  // defecto es lo de Festum Venti (crema y oro).
  struct Style {
    Color title{232, 214, 180, 255};
    Color subtitle{170, 150, 130, 255};
    Color menu{176, 168, 160, 255};
    Color menuSelected{236, 204, 128, 255};
    Color menuDisabled{92, 86, 84, 255};
    Color hint{130, 120, 112, 255};
    Color veil{8, 6, 14, 255}; // el velo que oscurece el fondo detras del titulo y el menu
    float menuMess = 0.0f;
  };
  Style style;

  std::string introMusic;
  float introVolume = 0.4f;
  std::vector<IntroShot> intro;

  using Flags = std::unordered_map<std::string, int>;
  // La pantalla de inicio de una partida (por su escena y sus banderas). Siempre hay una.
  const TitleScreen& TitleFor(const std::string& scene, const Flags& flags) const;
  // Si 'scene' es la de la pantalla de inicio 't' (o empieza por uno de sus prefijos): ahi valen
  // su "orbit" y su "spin".
  static bool Here(const TitleScreen& t, const std::string& scene);
  // Como se llama el sitio de una partida en la lista.
  std::string PlaceOf(const std::string& scene, const Flags& flags) const;
  float TitleMess(const Flags& flags) const;
};

// Lee games/<id>/game.json. Lanza std::runtime_error si falta algo que hace falta (o no tiene
// ninguna pantalla de inicio).
GameConfig LoadGameConfig(const std::filesystem::path& file);

} // namespace retro
