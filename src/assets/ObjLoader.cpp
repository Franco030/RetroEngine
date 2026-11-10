#include "assets/ObjLoader.hpp"

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <stdexcept>
#include <string_view>
#include <unordered_map>

namespace retro {

namespace {

namespace fs = std::filesystem;

std::string ReadFile(const std::string& path) {
  std::ifstream in(path, std::ios::binary);
  if (!in)
    throw std::runtime_error("No se pudo abrir: " + path);
  std::ostringstream ss;
  ss << in.rdbuf();
  return ss.str();
}

std::string_view Trim(std::string_view s) {
  while (!s.empty() && (s.front() == ' ' || s.front() == '\t'))
    s.remove_prefix(1);
  while (!s.empty() && (s.back() == ' ' || s.back() == '\t' || s.back() == '\r'))
    s.remove_suffix(1);
  return s;
}

// Siguiente palabra de 's' (la quita de 's').
std::string_view Token(std::string_view& s) {
  s = Trim(s);
  std::size_t end = 0;
  while (end < s.size() && s[end] != ' ' && s[end] != '\t')
    ++end;
  std::string_view t = s.substr(0, end);
  s.remove_prefix(end);
  return t;
}

float Number(std::string_view t, const std::string& where) {
  const std::string tmp(t); // strtof necesita el final de cadena
  char* end = nullptr;
  const float v = std::strtof(tmp.c_str(), &end);
  if (tmp.empty() || end != tmp.c_str() + tmp.size())
    throw std::runtime_error(where + ": numero no valido '" + tmp + "'");
  return v;
}

struct Material {
  std::string texture;
  Color color = WHITE;
};

std::unordered_map<std::string, Material> ReadMtl(const fs::path& path) {
  std::unordered_map<std::string, Material> out;
  std::ifstream in(path);
  if (!in)
    return out; // sin .mtl: todo sin textura (la pone la entidad con "texture")
  std::string line;
  Material* current = nullptr;
  while (std::getline(in, line)) {
    std::string_view rest = line;
    const std::string_view key = Token(rest);
    if (key == "newmtl") {
      current = &out[std::string(Trim(rest))];
    } else if (current && key == "map_Kd") {
      current->texture = (path.parent_path() / std::string(Trim(rest))).lexically_normal().string();
    } else if (current && key == "Kd") {
      const std::string where = path.string();
      auto channel = [&](std::string_view t) {
        return static_cast<unsigned char>(std::lround(std::clamp(Number(t, where), 0.0f, 1.0f) * 255.0f));
      };
      const std::string_view r = Token(rest), g = Token(rest), b = Token(rest);
      current->color = {channel(r), channel(g), channel(b), 255};
    }
  }
  return out;
}

} // namespace

ObjData LoadObjData(const std::string& path) {
  const std::string text = ReadFile(path);
  std::vector<Vector3> v, vn;
  std::vector<Vector2> vt;
  std::unordered_map<std::string, Material> materials;
  std::unordered_map<std::string, std::size_t> partOf; // material -> indice en parts
  ObjData data;
  ObjData::Part* part = nullptr;

  auto usePart = [&](const std::string& name) {
    auto [it, nuevo] = partOf.try_emplace(name, data.parts.size());
    if (nuevo) {
      ObjData::Part p;
      p.material = name;
      if (auto m = materials.find(name); m != materials.end()) {
        p.texture = m->second.texture;
        p.color = m->second.color;
      }
      data.parts.push_back(std::move(p));
    }
    part = &data.parts[it->second];
  };

  std::size_t pos = 0;
  int number = 0;
  struct Corner {
    int v = 0, vt = -1, vn = -1;
  };
  std::vector<Corner> face;
  while (pos < text.size()) {
    std::size_t nl = text.find('\n', pos);
    if (nl == std::string::npos)
      nl = text.size();
    std::string_view line(text.data() + pos, nl - pos);
    pos = nl + 1;
    ++number;
    const std::string where = path + ":" + std::to_string(number);

    if (line.rfind("# retro:col ", 0) == 0) {
      std::string_view rest = line.substr(12);
      ModelBox b;
      float* f[7] = {&b.min.x, &b.min.y, &b.min.z, &b.max.x, &b.max.y, &b.max.z, &b.yaw};
      for (float* x : f)
        *x = Number(Token(rest), where);
      if (b.min.x >= b.max.x || b.min.y >= b.max.y || b.min.z >= b.max.z)
        throw std::runtime_error(where + ": caja de colision mal escrita (min x y z, max x y z, yaw)");
      data.boxes.push_back(b);
      continue;
    }

    std::string_view rest = line;
    const std::string_view key = Token(rest);
    if (key == "v") {
      const float x = Number(Token(rest), where), y = Number(Token(rest), where);
      v.push_back({x, y, Number(Token(rest), where)});
    } else if (key == "vt") {
      const float s = Number(Token(rest), where);
      vt.push_back({s, Number(Token(rest), where)});
    } else if (key == "vn") {
      const float x = Number(Token(rest), where), y = Number(Token(rest), where);
      vn.push_back({x, y, Number(Token(rest), where)});
    } else if (key == "mtllib") {
      materials = ReadMtl(fs::path(path).parent_path() / std::string(Trim(rest)));
    } else if (key == "usemtl") {
      usePart(std::string(Trim(rest)));
    } else if (key == "f") {
      if (!part)
        usePart("");
      face.clear();
      for (std::string_view t = Token(rest); !t.empty(); t = Token(rest)) {
        // v | v/vt | v//vn | v/vt/vn; indices desde 1, o negativos desde el final
        int idx[3] = {0, 0, 0};
        int k = 0;
        std::size_t start = 0;
        for (std::size_t i = 0; i <= t.size() && k < 3; ++i) {
          if (i == t.size() || t[i] == '/') {
            if (i > start)
              idx[k] = std::atoi(std::string(t.substr(start, i - start)).c_str());
            ++k;
            start = i + 1;
          }
        }
        auto resolve = [&](int i, std::size_t count) {
          const long r = i > 0 ? i - 1 : static_cast<long>(count) + i;
          if (r < 0 || r >= static_cast<long>(count))
            throw std::runtime_error(where + ": indice fuera de rango");
          return static_cast<int>(r);
        };
        Corner c;
        c.v = resolve(idx[0], v.size());
        if (idx[1] != 0)
          c.vt = resolve(idx[1], vt.size());
        if (idx[2] != 0)
          c.vn = resolve(idx[2], vn.size());
        face.push_back(c);
      }
      if (face.size() < 3)
        throw std::runtime_error(where + ": cara con menos de 3 vertices");
      for (std::size_t i = 1; i + 1 < face.size(); ++i) { // abanico
        const Corner tri[3] = {face[0], face[i], face[i + 1]};
        // normal plana del triangulo (para las esquinas que no traen la suya)
        const Vector3 a = v[tri[0].v], b = v[tri[1].v], c = v[tri[2].v];
        Vector3 n{(b.y - a.y) * (c.z - a.z) - (b.z - a.z) * (c.y - a.y),
                  (b.z - a.z) * (c.x - a.x) - (b.x - a.x) * (c.z - a.z),
                  (b.x - a.x) * (c.y - a.y) - (b.y - a.y) * (c.x - a.x)};
        const float len = std::sqrt(n.x * n.x + n.y * n.y + n.z * n.z);
        n = len > 1e-12f ? Vector3{n.x / len, n.y / len, n.z / len} : Vector3{0.0f, 1.0f, 0.0f};
        for (const Corner& corner : tri) {
          const Vector3 p = v[corner.v];
          part->positions.insert(part->positions.end(), {p.x, p.y, p.z});
          const Vector2 uv = corner.vt >= 0 ? vt[corner.vt] : Vector2{0.0f, 0.0f};
          part->texcoords.insert(part->texcoords.end(), {uv.x, 1.0f - uv.y});
          const Vector3 nn = corner.vn >= 0 ? vn[corner.vn] : n;
          part->normals.insert(part->normals.end(), {nn.x, nn.y, nn.z});
        }
      }
    }
  }
  std::erase_if(data.parts, [](const ObjData::Part& p) { return p.positions.empty(); });
  if (data.parts.empty())
    throw std::runtime_error(path + ": el modelo no tiene caras");
  return data;
}

} // namespace retro
