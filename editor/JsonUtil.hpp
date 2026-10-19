#pragma once

#include "JsonFormat.hpp"

#include <nlohmann/json.hpp>
#include <raylib.h>

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <string>

namespace editor {

// ordered_json conserva el orden de las claves del archivo: al guardar, el cambio en el
// archivo es minimo (no se reordena todo alfabeticamente).
using Json = nlohmann::ordered_json;

// Entero si el valor lo es (el archivo queda "7", no "7.0"); si no, redondeado a 3 decimales.
inline Json Number(double v) {
  const double r = std::round(v * 1000.0) / 1000.0;
  if (std::fabs(r - std::round(r)) < 1e-9)
    return Json(static_cast<long long>(std::llround(r)));
  return Json(r);
}

inline Json Vec3Json(Vector3 v) { return Json::array({Number(v.x), Number(v.y), Number(v.z)}); }
inline Json Vec2Json(Vector2 v) { return Json::array({Number(v.x), Number(v.y)}); }

inline bool IsNumberArray(const Json& a, std::size_t n) {
  if (!a.is_array() || a.size() != n)
    return false;
  for (const Json& v : a) {
    if (!v.is_number())
      return false;
  }
  return true;
}

// --- lectura (con valor por defecto si falta o no tiene la forma esperada) ---
inline Vector3 ReadVec3(const Json* obj, const char* key, Vector3 def) {
  if (!obj || !obj->is_object() || !obj->contains(key))
    return def;
  const Json& a = obj->at(key);
  if (!IsNumberArray(a, 3))
    return def;
  return {a[0].get<float>(), a[1].get<float>(), a[2].get<float>()};
}

inline Vector2 ReadVec2(const Json* obj, const char* key, Vector2 def) {
  if (!obj || !obj->is_object() || !obj->contains(key))
    return def;
  const Json& a = obj->at(key);
  if (!IsNumberArray(a, 2))
    return def;
  return {a[0].get<float>(), a[1].get<float>()};
}

inline float ReadNumber(const Json* obj, const char* key, float def) {
  if (!obj || !obj->is_object() || !obj->contains(key) || !obj->at(key).is_number())
    return def;
  return obj->at(key).get<float>();
}

inline bool ReadBool(const Json* obj, const char* key, bool def) {
  if (!obj || !obj->is_object() || !obj->contains(key) || !obj->at(key).is_boolean())
    return def;
  return obj->at(key).get<bool>();
}

inline std::string ReadString(const Json* obj, const char* key) {
  if (!obj || !obj->is_object() || !obj->contains(key) || !obj->at(key).is_string())
    return {};
  return obj->at(key).get<std::string>();
}

inline Color ReadColor(const Json* obj, const char* key, Color def) {
  if (!obj || !obj->is_object() || !obj->contains(key))
    return def;
  const Json& a = obj->at(key);
  if (!a.is_array() || (a.size() != 3 && a.size() != 4))
    return def;
  for (const Json& v : a) {
    if (!v.is_number())
      return def;
  }
  auto channel = [](const Json& v) {
    return static_cast<unsigned char>(std::clamp(v.get<int>(), 0, 255));
  };
  return {channel(a[0]), channel(a[1]), channel(a[2]),
          a.size() == 4 ? channel(a[3]) : static_cast<unsigned char>(255)};
}

// --- escritura ---
// 'parent' nulo = la propia raiz; si no, un objeto hijo ("fog", "light"...) que se crea al
// escribir. Con erase = true se quita la clave (y el hijo, si se queda vacio): asi el archivo
// no se llena de valores que ya son los de por defecto.
inline const Json* Src(const Json& root, const char* parent) {
  if (!parent)
    return &root;
  if (root.is_object() && root.contains(parent) && root.at(parent).is_object())
    return &root.at(parent);
  return nullptr;
}

inline void Store(Json& root, const char* parent, const char* key, const Json& value, bool erase) {
  if (!parent) {
    if (erase)
      root.erase(key);
    else
      root[key] = value;
    return;
  }
  if (erase) {
    if (root.contains(parent) && root.at(parent).is_object()) {
      root[parent].erase(key);
      if (root.at(parent).empty())
        root.erase(parent);
    }
    return;
  }
  if (!root.contains(parent) || !root.at(parent).is_object())
    root[parent] = Json::object();
  root[parent][key] = value;
}

inline bool SameNumber(float a, float b) {
  return std::llround(a * 1000.0f) == std::llround(b * 1000.0f);
}
inline bool SameVec3(Vector3 a, Vector3 b) {
  return SameNumber(a.x, b.x) && SameNumber(a.y, b.y) && SameNumber(a.z, b.z);
}
inline bool SameVec2(Vector2 a, Vector2 b) { return SameNumber(a.x, b.x) && SameNumber(a.y, b.y); }

} // namespace editor
