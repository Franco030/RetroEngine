#pragma once

#include <filesystem>
#include <string>
#include <unordered_map>
#include <vector>

namespace retro {

// Los idiomas. Los textos se escriben en el idioma de origen (el de las escenas, los dialogos y el
// codigo: espanol) y cada traduccion es una tabla "texto de origen -> texto traducido" en
// lang/<codigo>.json: la del motor (engine/assets/lang, los menus del runtime) y la del juego
// (games/<id>/assets/lang, sus dialogos, mensajes, avisos...). Lo que no esta en la tabla sale en
// el idioma de origen, asi un texto nuevo nunca desaparece: solo le falta su traduccion.
namespace lang {

// La tabla en uso (vacia = el idioma de origen). Se cambia al elegir otro idioma.
void Use(std::unordered_map<std::string, std::string> table);

// El texto en el idioma elegido (o el mismo, si no tiene traduccion).
const std::string& Tr(const std::string& text);
// Para literales del codigo: Tr("Continuar").
const char* Tr(const char* text);

// Lee lang/<code>.json de cada carpeta (en orden: las de despues tapan a las de antes). Un archivo
// que no existe no cuenta; uno roto avisa por consola y no cuenta.
std::unordered_map<std::string, std::string> LoadTable(const std::vector<std::filesystem::path>& roots,
                                                       const std::string& code);

} // namespace lang

} // namespace retro
