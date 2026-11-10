#pragma once

#include "world/Logic.hpp"

#include <nlohmann/json_fwd.hpp>

#include <string>
#include <vector>

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

void LoadSceneFromJson(const nlohmann::json& root, const std::string& relativePath,
                       RetroEngine& engine, Scene& scene, RetroCamera& camera);

// Lo mismo que usan las escenas, para otros archivos con logica (los dialogos):
// j[key] es una condicion o un arreglo de ellas (todas deben cumplirse); falta = lista vacia.
std::vector<Condition> ParseConditions(const nlohmann::json& j, const char* key = "if");
// Una accion: un objeto con UNA clave de accion. Lanza std::runtime_error si esta mal.
Action ParseAction(const nlohmann::json& ja);

} // namespace retro