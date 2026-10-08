#include "graphics/MeshFactory.hpp"

#include <algorithm>
#include <cstddef>

namespace retro {

Mesh MakeTiledPlane(float width, float length, int subdivisions, float tilesU, float tilesV) {
  const int res = std::max(subdivisions, 1);

  Mesh mesh = GenMeshPlane(width, length, res, res);

  for (int i = 0; i < mesh.vertexCount; ++i) {
    mesh.texcoords[2 * i + 0] *= tilesU;
    mesh.texcoords[2 * i + 1] *= tilesV;
  }

  UpdateMeshBuffer(mesh, 1, mesh.texcoords,
                   static_cast<int>(static_cast<std::size_t>(mesh.vertexCount) * 2 * sizeof(float)),
                   0);

  return mesh;
}

} // namespace retro
