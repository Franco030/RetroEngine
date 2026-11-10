#pragma once

#include "assets/ObjLoader.hpp"
#include "assets/Resources.hpp"

#include <map>
#include <memory>
#include <string>

namespace retro {

class RetroEngine;

// Las texturas que ya se subieron (con mipmaps), por ruta: las comparte todo lo que suba quien la tiene.
using TextureCache = std::map<std::string, std::shared_ptr<TextureResource>>;

// Sube a la GPU una malla hecha en codigo (gen::Builder): sus texturas en REPEAT y con mipmaps (de
// cerca, pixeles; de lejos, el promedio: sin ellos lo lejano hierve como granito) y el shader con luz.
std::shared_ptr<ModelResource> UploadMesh(RetroEngine& engine, TextureCache& cache, const ObjData& data);

} // namespace retro
