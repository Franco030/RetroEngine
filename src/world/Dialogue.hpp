#pragma once

#include "game/GameState.hpp"
#include "world/Logic.hpp"

#include <cstddef>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

namespace retro {

// Una respuesta posible de un dialogo: paginas que se pasan con E y acciones al terminar.
struct DialogueEntry {
  std::string key;                    // para la bandera "dlg:<dialogo>.<key>" (por defecto su indice)
  std::vector<Condition> conditions;  // "if": cuando puede salir
  bool once = false;                  // "once": solo la primera vez que se elige
  std::vector<std::string> lines;     // "lines": cada una es una pagina
  std::vector<Action> actions;        // "do": al cerrar la ultima pagina
};

struct Dialogue {
  std::string speaker;
  std::vector<DialogueEntry> entries; // en orden: gana la primera que se pueda decir
};

// Carga un dialogo de 'relativePath' (relativo a assets/). Lanza std::runtime_error si esta mal.
//   { "speaker": "Sepulturero",
//     "entries": [ { "once": true, "lines": ["Hola.", "Toma la llave."],
//                    "do": [ { "set_flag": "llave" } ] },
//                  { "if": { "flag": "llave" }, "lines": ["Ya tienes la llave."] } ] }
Dialogue LoadDialogue(const std::string& root, const std::string& relativePath);

// Banderas que guardan la memoria de un dialogo (persisten con la partida):
//   dlg:<id>          veces que se ha hablado con el
//   dlg:<id>.<key>    1 si esa entrada 'once' ya salio
std::string DialogueCountFlag(const std::string& id);
std::string DialogueEntryFlag(const std::string& id, const std::string& key);

// Lleva el dialogo en curso. Se abre con la accion { "dialogue": "<id>" } (un comando mas, como
// los demas que ejecuta ActionRunner) y escribe cada pagina como el mensaje de GameState, que es
// lo que la interfaz dibuja.
class DialogueSystem {
public:
  explicit DialogueSystem(std::string assetsRoot) : root_(std::move(assetsRoot)) {}

  // Elige la entrada, la marca y muestra su primera pagina. false si no hay nada que decir (o
  // el archivo falla: se avisa por consola).
  bool Start(const std::string& id, GameState& state);

  bool Active() const noexcept { return entry_ != nullptr; }
  bool LastPage() const noexcept { return entry_ && page_ + 1 >= entry_->lines.size(); }

  // E con la pagina ya escrita: muestra la siguiente o, si era la ultima, cierra el dialogo y
  // devuelve sus acciones finales (para ActionRunner).
  std::vector<Action> Advance(GameState& state);

  // Si otro mensaje reemplazo al del dialogo (un aviso, una escena nueva), lo da por cerrado.
  void Update(const GameState& state);

  // Olvida lo cargado (F5): se vuelve a leer del disco la proxima vez.
  void Reload();

private:
  const Dialogue* Get(const std::string& id);
  void ShowPage(GameState& state);

  std::string root_;
  std::unordered_map<std::string, Dialogue> cache_; // nodos estables: los punteros siguen valiendo
  const Dialogue* dialogue_ = nullptr;
  const DialogueEntry* entry_ = nullptr;
  std::size_t page_ = 0;
  unsigned messageId_ = 0; // el mensaje que puso este dialogo
};

} // namespace retro
