#include <raylib.h>

#include <exception>
#include <iostream>
#include <string>

#include "EditorApp.hpp"
#include "core/RetroEngine.hpp"

#ifndef RETRO_PROJECT_ASSETS_DIR
#define RETRO_PROJECT_ASSETS_DIR "assets"
#endif

int main(int argc, char** argv) {
  try {
    // RetroEditor [--assets <carpeta>] [--scene <scenes/x.json>]
    std::string assets = RETRO_PROJECT_ASSETS_DIR;
    std::string scene;
    for (int i = 1; i + 1 < argc; ++i) {
      if (std::string(argv[i]) == "--assets")
        assets = argv[i + 1];
      if (std::string(argv[i]) == "--scene")
        scene = argv[i + 1];
    }
    if (!assets.empty() && assets.back() != '/' && assets.back() != '\\')
      assets += '/';

    retro::EngineConfig config;
    config.title = "RetroEditor";
    config.windowWidth = 1600;
    config.windowHeight = 900;
    config.assetRoot = assets;
    config.uiFont = "";

    retro::RetroEngine engine(config);
    {
      editor::EditorApp app(engine, scene);
      app.Run();
    }
  } catch (const std::exception& e) {
    std::cerr << "Error fatal: " << e.what() << '\n';
    return 1;
  }
  return 0;
}