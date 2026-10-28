#include "world/Dialogue.hpp"

#include "world/SceneLoader.hpp"

#include <nlohmann/json.hpp>

#include <fstream>
#include <iostream>
#include <stdexcept>

namespace retro {

namespace {

// Lo bastante largo para no cerrarse solo: la pagina se quita con E.
constexpr float kPageSeconds = 1.0e6f;

} // namespace

Dialogue LoadDialogue(const std::string& root, const std::string& relativePath) {
  std::ifstream file(root + relativePath);
  if (!file)
    throw std::runtime_error("no se pudo abrir " + relativePath);

  const nlohmann::json j = nlohmann::json::parse(file);
  Dialogue d;
  d.speaker = j.value("speaker", std::string());
  const auto& entries = j.at("entries");
  if (!entries.is_array() || entries.empty())
    throw std::runtime_error(relativePath + ": 'entries' debe ser un arreglo no vacio");

  for (std::size_t i = 0; i < entries.size(); ++i) {
    const nlohmann::json& je = entries[i];
    try {
      DialogueEntry e;
      e.key = je.value("key", std::to_string(i));
      e.conditions = ParseConditions(je);
      e.once = je.value("once", false);
      for (const auto& line : je.at("lines"))
        e.lines.push_back(line.get<std::string>());
      if (e.lines.empty())
        throw std::runtime_error("'lines' no puede estar vacio");
      if (je.contains("do")) {
        for (const auto& ja : je.at("do"))
          e.actions.push_back(ParseAction(ja));
      }
      d.entries.push_back(std::move(e));
    } catch (const std::exception& ex) {
      throw std::runtime_error(relativePath + ", entrada " + std::to_string(i) + ": " + ex.what());
    }
  }
  return d;
}

std::string DialogueCountFlag(const std::string& id) { return "dlg:" + id; }

std::string DialogueEntryFlag(const std::string& id, const std::string& key) {
  return "dlg:" + id + "." + key;
}

const Dialogue* DialogueSystem::Get(const std::string& id) {
  if (const auto it = cache_.find(id); it != cache_.end())
    return &it->second;
  try {
    auto [it, _] = cache_.emplace(id, LoadDialogue(root_, "dialogues/" + id + ".json"));
    return &it->second;
  } catch (const std::exception& e) {
    std::cerr << "[dialogo] " << e.what() << '\n';
    return nullptr;
  }
}

bool DialogueSystem::Start(const std::string& id, GameState& state) {
  const Dialogue* d = Get(id);
  if (!d)
    return false;

  for (const DialogueEntry& e : d->entries) {
    const std::string spent = DialogueEntryFlag(id, e.key);
    if ((e.once && state.Get(spent) != 0) || !AllHold(e.conditions, state))
      continue;
    if (e.once)
      state.Set(spent, 1);
    state.Set(DialogueCountFlag(id), state.Get(DialogueCountFlag(id)) + 1);
    dialogue_ = d;
    entry_ = &e;
    page_ = 0;
    ShowPage(state);
    return true;
  }
  return false; // ninguna entrada se cumple: no dice nada
}

void DialogueSystem::ShowPage(GameState& state) {
  state.ShowMessage(ExpandFlags(entry_->lines[page_], state), kPageSeconds, dialogue_->speaker);
  messageId_ = state.MessageId();
}

std::vector<Action> DialogueSystem::Advance(GameState& state) {
  if (!entry_)
    return {};
  if (++page_ < entry_->lines.size()) {
    ShowPage(state);
    return {};
  }
  std::vector<Action> actions = entry_->actions; // copia: Start (desde ellas) puede cambiar entry_
  dialogue_ = nullptr;
  entry_ = nullptr;
  state.ClearMessage();
  return actions;
}

void DialogueSystem::Update(const GameState& state) {
  if (entry_ && (!state.HasMessage() || state.MessageId() != messageId_)) {
    dialogue_ = nullptr;
    entry_ = nullptr;
  }
}

void DialogueSystem::Reload() {
  dialogue_ = nullptr;
  entry_ = nullptr;
  cache_.clear();
}

} // namespace retro
