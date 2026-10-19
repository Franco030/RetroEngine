#pragma once

// Escritura "bonita" de JSON, pensada para que los archivos de escena guardados por el editor
// se parezcan a los escritos a mano: los objetos y arreglos cortos van en una linea
//   "camera": { "position": [7, 5, 9], "target": [0, 1, 0], "fov": 60 }
// y los largos se expanden con sangria de 2 espacios. nlohmann::json::dump(2) pondria cada
// numero de cada vector en su propia linea.
//
// Es una plantilla sobre el tipo JSON (sin incluir nlohmann), para poder probarla aislada.

#include <algorithm>
#include <cstddef>
#include <string>

namespace editor::jsonfmt {

constexpr std::size_t kInlineMax = 100;   // maximo de caracteres de un valor en una sola linea
constexpr int kInlineMaxDepth    = 3;     // y como mucho 3 niveles de anidacion

template <class J>
bool IsScalar(const J& j) {
    return !j.is_object() && !j.is_array();
}

template <class J>
int Depth(const J& j) {
    int d = 0;
    if (j.is_array()) {
        for (const auto& v : j) d = std::max(d, Depth(v));
        return d + 1;
    }
    if (j.is_object()) {
        for (const auto& kv : j.items()) d = std::max(d, Depth(kv.value()));
        return d + 1;
    }
    return 0;
}

// Arreglos "[a, b]" y objetos "{ "k": v }", como en los archivos escritos a mano.
template <class J>
std::string InlineText(const J& j) {
    if (IsScalar(j)) return j.dump();

    std::string s;
    bool first = true;
    if (j.is_array()) {
        s = "[";
        for (const auto& v : j) {
            if (!first) s += ", ";
            first = false;
            s += InlineText(v);
        }
        s += "]";
    } else {
        if (j.size() == 0) return "{}";
        s = "{ ";
        for (const auto& kv : j.items()) {
            if (!first) s += ", ";
            first = false;
            s += J(kv.key()).dump() + ": " + InlineText(kv.value());
        }
        s += " }";
    }
    return s;
}

template <class J>
void Pretty(const J& j, int indent, std::string& out, bool forceExpand = false) {
    if (IsScalar(j)) {
        out += j.dump();
        return;
    }
    if (!forceExpand && Depth(j) <= kInlineMaxDepth) {
        const std::string inl = InlineText(j);
        if (inl.size() <= kInlineMax) {
            out += inl;
            return;
        }
    }

    const std::string pad(static_cast<std::size_t>(indent + 2), ' ');
    const std::string padEnd(static_cast<std::size_t>(indent), ' ');
    bool first = true;

    if (j.is_array()) {
        if (j.size() == 0) { out += "[]"; return; }
        out += "[\n";
        for (const auto& v : j) {
            if (!first) out += ",\n";
            first = false;
            out += pad;
            Pretty(v, indent + 2, out);
        }
        out += "\n" + padEnd + "]";
    } else {
        if (j.size() == 0) { out += "{}"; return; }
        out += "{\n";
        for (const auto& kv : j.items()) {
            if (!first) out += ",\n";
            first = false;
            out += pad + J(kv.key()).dump() + ": ";
            Pretty(kv.value(), indent + 2, out);
        }
        out += "\n" + padEnd + "}";
    }
}

// El objeto raiz siempre se expande, y el archivo acaba en salto de linea.
template <class J>
std::string FormatJson(const J& j) {
    std::string out;
    Pretty(j, 0, out, true);
    out += '\n';
    return out;
}

} // namespace editor::jsonfmt
