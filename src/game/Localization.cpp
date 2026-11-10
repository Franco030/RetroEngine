#include "game/Localization.hpp"

#include <nlohmann/json.hpp>

#include <exception>
#include <fstream>
#include <iostream>
#include <utility>

namespace retro::lang {

namespace {

std::unordered_map<std::string, std::string>& Table() {
  static std::unordered_map<std::string, std::string> table;
  return table;
}

} // namespace

void Use(std::unordered_map<std::string, std::string> table) { Table() = std::move(table); }

const std::string& Tr(const std::string& text) {
  const auto& table = Table();
  if (table.empty() || text.empty())
    return text;
  const auto it = table.find(text);
  return it != table.end() ? it->second : text;
}

const char* Tr(const char* text) {
  const auto& table = Table();
  if (table.empty())
    return text;
  const auto it = table.find(text);
  return it != table.end() ? it->second.c_str() : text;
}

std::unordered_map<std::string, std::string> LoadTable(const std::vector<std::filesystem::path>& roots,
                                                       const std::string& code) {
  std::unordered_map<std::string, std::string> table;
  for (const std::filesystem::path& root : roots) {
    const std::filesystem::path file = root / "lang" / (code + ".json");
    std::ifstream in(file);
    if (!in)
      continue;
    try {
      const nlohmann::json j = nlohmann::json::parse(in);
      for (const auto& [from, to] : j.items()) {
        if (to.is_string() && !to.get<std::string>().empty())
          table[from] = to.get<std::string>();
      }
    } catch (const std::exception& e) {
      std::cerr << "[idioma] " << file.generic_string() << ": " << e.what() << '\n';
    }
  }
  return table;
}

} // namespace retro::lang
