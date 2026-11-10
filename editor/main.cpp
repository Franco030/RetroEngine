#include <raylib.h>

#include <exception>
#include <iostream>
#include <string>

#include "EditorApp.hpp"
#include "core/RetroEngine.hpp"

// CMake pone las carpetas del repo: games/ (un juego por carpeta) y engine/assets.
#ifndef RETRO_GAMES_DIR
#define RETRO_GAMES_DIR "games"
#endif
#ifndef RETRO_ENGINE_ASSETS_DIR
#define RETRO_ENGINE_ASSETS_DIR "engine/assets"
#endif
#ifndef RETRO_DEFAULT_GAME
#define RETRO_DEFAULT_GAME ""
#endif

int main(int argc, char** argv) {
  try {
    // RetroEditor [--game <id> | --assets <carpeta>] [--scene <scenes/x.json>]
    // --game: el juego de games/<id>; --assets: la carpeta de assets de un juego (su game.json va
    // en la carpeta de arriba). Sin ninguno, el juego por defecto.
    std::string assets = std::string(RETRO_GAMES_DIR) + "/" + RETRO_DEFAULT_GAME + "/assets";
    std::string scene;
    for (int i = 1; i + 1 < argc; ++i) {
      const std::string a = argv[i];
      if (a == "--assets")
        assets = argv[i + 1];
      else if (a == "--game")
        assets = std::string(RETRO_GAMES_DIR) + "/" + argv[i + 1] + "/assets";
      else if (a == "--scene")
        scene = argv[i + 1];
    }
    if (!assets.empty() && assets.back() != '/' && assets.back() != '\\')
      assets += '/';

    retro::EngineConfig config;
    config.title = "RetroEditor";
    config.windowWidth = 1600;
    config.windowHeight = 900;
    config.assetRoot = assets;
    config.engineRoot = RETRO_ENGINE_ASSETS_DIR;
    config.uiFont = "";

    retro::RetroEngine engine(config);
    {
      editor::EditorApp app(engine, scene, RETRO_GAMES_DIR);
      app.Run();
    }
  } catch (const std::exception& e) {
    std::cerr << "Error fatal: " << e.what() << '\n';
    return 1;
  }
  return 0;
}
