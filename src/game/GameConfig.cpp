#include "game/GameConfig.hpp"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <cmath>
#include <fstream>
#include <stdexcept>

namespace retro {

namespace {

using json = nlohmann::json;

Vector3 Vec3(const json& j, const char* key, Vector3 def) {
  if (!j.contains(key))
    return def;
  const json& a = j.at(key);
  if (!a.is_array() || a.size() != 3)
    throw std::runtime_error(std::string("'") + key + "' debe ser [x, y, z]");
  return {a.at(0).get<float>(), a.at(1).get<float>(), a.at(2).get<float>()};
}

std::vector<std::string> Strings(const json& j, const char* key) {
  std::vector<std::string> out;
  if (j.contains(key)) {
    for (const json& s : j.at(key))
      out.push_back(s.get<std::string>());
  }
  return out;
}

bool Starts(const std::string& s, const std::string& prefix) { return s.rfind(prefix, 0) == 0; }

// [r, g, b] o [r, g, b, a], de 0 a 255.
Color ReadColor(const json& j, const char* key, Color def) {
  if (!j.contains(key))
    return def;
  const json& a = j.at(key);
  if (!a.is_array() || (a.size() != 3 && a.size() != 4))
    throw std::runtime_error(std::string("'") + key + "' debe ser [r, g, b] o [r, g, b, a]");
  auto c = [&](std::size_t i) { return static_cast<unsigned char>(std::clamp(a.at(i).get<int>(), 0, 255)); };
  return {c(0), c(1), c(2), a.size() == 4 ? c(3) : static_cast<unsigned char>(255)};
}

} // namespace

const GameConfig::TitleScreen& GameConfig::TitleFor(const std::string& scene, const Flags& flags) const {
  auto has = [&](const std::string& f) {
    const auto it = flags.find(f);
    return it != flags.end() && it->second != 0;
  };
  for (const TitleScreen& t : titleScreens) {
    if (std::any_of(t.prefixes.begin(), t.prefixes.end(), [&](const std::string& p) { return Starts(scene, p); }) ||
        std::any_of(t.flags.begin(), t.flags.end(), has))
      return t;
  }
  return titleScreens.back();
}

bool GameConfig::Here(const TitleScreen& t, const std::string& scene) {
  return scene == t.scene ||
         std::any_of(t.prefixes.begin(), t.prefixes.end(), [&](const std::string& p) { return Starts(scene, p); });
}

std::string GameConfig::PlaceOf(const std::string& scene, const Flags& flags) const {
  for (const Place& p : places) {
    if (Starts(scene, p.prefix))
      return p.name;
  }
  return TitleFor(scene, flags).place;
}

float GameConfig::TitleMess(const Flags& flags) const {
  int n = 0;
  if (!distortionFlag.empty()) {
    if (const auto it = flags.find(distortionFlag); it != flags.end())
      n = std::max(0, it->second);
  }
  return distortionBase * std::pow(distortionFactor, static_cast<float>(n));
}

GameConfig LoadGameConfig(const std::filesystem::path& file) {
  std::ifstream in(file);
  if (!in)
    throw std::runtime_error("no se encuentra " + file.string());
  const json j = json::parse(in);

  GameConfig g;
  g.id = file.parent_path().filename().string();
  g.name = j.value("name", g.name);
  g.exe = j.value("exe", g.exe);
  g.savesFolder = j.value("saves", g.name);
  g.title = j.value("title", g.name);
  g.subtitle = j.value("subtitle", std::string());
  g.firstScene = j.at("firstScene").get<std::string>();
  g.itemsFile = j.value("items", std::string());
  if (j.contains("resolution")) {
    const json& jr = j.at("resolution");
    g.width = jr.at(0).get<int>();
    g.height = jr.at(1).get<int>();
    if (g.width < 160 || g.height < 120 || g.width > 3840 || g.height > 2160)
      throw std::runtime_error("'resolution' debe ser [ancho, alto] entre 160x120 y 3840x2160");
  }

  for (const json& jt : j.at("titleScreens")) {
    GameConfig::TitleScreen t;
    t.scene = jt.at("scene").get<std::string>();
    t.prefixes = Strings(jt, "prefixes");
    t.flags = Strings(jt, "flags");
    t.music = jt.value("music", std::string());
    if (jt.contains("orbit")) {
      const json& jo = jt.at("orbit");
      t.fixedOrbit = true;
      t.center = Vec3(jo, "center", t.center);
      t.radius = jo.value("radius", t.radius);
      t.height = jo.value("height", t.height);
    }
    if (jt.contains("spin")) {
      const json& js = jt.at("spin");
      t.spin = true;
      t.spinCenter = Vec3(js, "center", t.spinCenter);
      t.spinAxis = Vec3(js, "axis", t.spinAxis);
      t.spinEye = Vec3(js, "eye", t.spinEye);
      t.spinLook = Vec3(js, "look", t.spinLook);
      t.spinSpeed = js.value("speed", t.spinSpeed);
      if (t.spinAxis.x == 0.0f && t.spinAxis.y == 0.0f && t.spinAxis.z == 0.0f)
        throw std::runtime_error("'spin': 'axis' no puede ser [0, 0, 0]");
    }
    t.distorted = jt.value("distorted", false);
    t.place = jt.value("place", std::string());
    g.titleScreens.push_back(std::move(t));
  }
  if (g.titleScreens.empty())
    throw std::runtime_error("'titleScreens' necesita al menos una pantalla de inicio");

  if (j.contains("places")) {
    for (const json& jp : j.at("places"))
      g.places.push_back({jp.at("prefix").get<std::string>(), jp.at("name").get<std::string>()});
  }
  if (j.contains("distortion")) {
    const json& jd = j.at("distortion");
    g.distortionFlag = jd.value("flag", std::string());
    g.distortionBase = jd.value("base", g.distortionBase);
    g.distortionFactor = jd.value("factor", g.distortionFactor);
  }
  if (j.contains("style")) {
    const json& js = j.at("style");
    GameConfig::Style& st = g.style;
    st.title = ReadColor(js, "title", st.title);
    st.subtitle = ReadColor(js, "subtitle", st.subtitle);
    st.menu = ReadColor(js, "menu", st.menu);
    st.menuSelected = ReadColor(js, "menuSelected", st.menuSelected);
    st.menuDisabled = ReadColor(js, "menuDisabled", st.menuDisabled);
    st.hint = ReadColor(js, "hint", st.hint);
    st.veil = ReadColor(js, "veil", st.veil);
    st.menuMess = std::clamp(js.value("menuMess", st.menuMess), 0.0f, 1.0f);
  }
  if (j.contains("languages")) {
    for (const json& jl : j.at("languages"))
      g.languages.push_back({jl.at("code").get<std::string>(), jl.value("name", jl.at("code").get<std::string>())});
  }
  if (g.languages.empty())
    g.languages.push_back({"es", "Espanol"});
  if (j.contains("intro")) {
    const json& ji = j.at("intro");
    g.introMusic = ji.value("music", std::string());
    g.introVolume = ji.value("volume", g.introVolume);
    for (const json& js : ji.at("shots")) {
      GameConfig::IntroShot s;
      s.scene = js.at("scene").get<std::string>();
      s.eyeFrom = Vec3(js, "eyeFrom", s.eyeFrom);
      s.eyeTo = Vec3(js, "eyeTo", s.eyeFrom);
      s.lookFrom = Vec3(js, "lookFrom", s.lookFrom);
      s.lookTo = Vec3(js, "lookTo", s.lookFrom);
      s.seconds = js.value("seconds", s.seconds);
      s.text = js.value("text", std::string());
      if (s.seconds <= 0.0f)
        throw std::runtime_error("'intro': cada toma necesita 'seconds' positivo");
      g.intro.push_back(std::move(s));
    }
  }
  return g;
}

} // namespace retro
