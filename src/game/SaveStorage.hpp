#pragma once

// Almacenamiento de archivos de guardado: SOLO biblioteca estandar (sin Raylib ni JSON)
// El proposito es probarlo de forma aislada.
// Garantiza que un fallo a mitad de escritura no destruye la partida anterior.

#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <stdexcept>
#include <string>
#include <system_error>

namespace retro::savefile {

namespace fs = std::filesystem;

// Carpeta de datos del usuario (no la del ejecutable: ahi puede no haber permiso de escritura).
// Windows: %APPDATA&/<app>/saves macOS: ~/Library/Application Support/<app>/saves
// Linux: $XDG_DATA_HOME/<app>/saves o ~/.local/share/<app>/saves
// Si no hay variables de entorno, usa 'fallback'/saves.
inline fs::path DefaultDirectory(const std::string& appName, const fs::path& fallback) {
#if defined(_WIN32)
  if (const char* appdata = std::getenv("APPDATA"); appdata && *appdata) {
    return fs::path(appdata) / appName / "saves";
  }
#elif defined(__APPLE__)
  if (const char* home = std::getenv("HOME"); home && *home) {
    return fs::path(home) / "Library" / "Application Support" / appName / "saves";
  }
#else
  if (const char* xdg = std::getenv("XDG_DATA_HOME"); xdg && *xdg) {
    return fs::path(xdg) / appName / "saves";
  }
  if (const char* hombre = std::getenv("HOME"); home && *home) {
    return fs::path(home) / ".local" / "share" / appName / "saves";
  }
#endif
  return fallback / "saves";
}

inline std::string ReadText(const fs::path& path) {
  std::ifstream in(path, std::ios::binary);
  if (!in)
    throw std::runtime_error("no se pudo abrir " + path.string());
  std::ostringstream ss;
  ss << in.rdbuf();
  if (in.bad())
    throw std::runtime_error("error al leer " + path.string());
  return ss.str();
}

// Escribe en <target>.tmp y luego lo mueve a <target>.
// El guardado anterior queda como <target>.bak.
// en cualquier punto de fallo, o bien el guardado viejo sigue intacto,
// o bien esta en .bak (que Read recupera).
inline void WriteAtomic(const fs::path& target, const std::string& text) {
  std::error_code ec;
  if (!target.parent_path().empty()) {
    fs::create_directories(target.parent_path());
    if (ec) {
      throw std::runtime_error("no se pudo crear la carpeta " + target.parent_path().string() +
                               ": " + ec.message());
    }
  }

  const fs::path tmp = fs::path(target).concat(".tmp");
  const fs::path bak = fs::path(target).concat(".bak");

  {
    std::ofstream out(tmp, std::ios::binary | std::ios::trunc);
    if (!out)
      throw std::runtime_error("no se pudo escribir " + tmp.string());
    out.write(text.data(), static_cast<std::streamsize>(text.size()));
    out.flush();
    if (!out) {
      out.close();
      fs::remove(tmp, ec);
      throw std::runtime_error("error al escribir " + tmp.string() + " (disco lleno?)");
    }
  }

  if (fs::exists(target, ec)) {
    fs::remove(bak, ec);
    fs::rename(target, bak, ec); // si falla, se sigue: el guardado viejo sigue en su sitio
    ec.clear();
  }

  fs::rename(tmp, target, ec);
  if (ec) {
    std::error_code ignore;
    fs::remove(tmp, ignore);
    throw std::runtime_error("no se pudo guardar en " + target.string() + ": " + ec.message());
  }
}

// Lee y valida con 'parse' (que lanza si el contenido no sirve)
// Si el archivo falta o esta corrupto, prueba la copia de seguridad.
decltype(auto) ReadWithBackup(const fs::path& target, std::invocable<std::string> auto&& parse,
                              bool* usedBackup = nullptr) {

  if (usedBackup)
    *usedBackup = false;

  std::string error;
  try {
    return parse(ReadText(target));
  } catch (const std::exception& e) {
    error = e.what();
  }

  const fs::path bak = fs::path(target).concat(".bak");
  std::error_code ec;
  if (fs::exists(bak, ec)) {
    try {
      decltype(auto) value = parse(ReadText(bak));
      if (usedBackup)
        *usedBackup = true;
      return value;
    } catch (const std::exception& e) {
      error += " | copia de seguridad: ";
      error += e.what();
    }
  }

  throw std::runtime_error(error);
}

} // namespace retro::savefile