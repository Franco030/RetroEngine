#pragma once

#include <string>

namespace retro {

class RetroEngine;
class Scene;
class RetroCamera;

// Carga un JSON de escena (ruta relativa a assets/) y lo aplica:
// - Añade las entidades a 'scene'
// - Configura 'camera' si el archivo trae "camera"
// - Ajusta la niebla del shader si trae "fog"
// Lanza std::runtime_error con el archivo y la entidad que fallo
void LoadScene(const std::string& relativePath, RetroEngine& engine, Scene& scene,
               RetroCamera& camera);

} // namespace retro