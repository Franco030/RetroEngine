#pragma once

#include <raylib.h>

#include <string>
#include <vector>

namespace retro {

// Caja de colision que el propio modelo trae en su .obj, en una linea de comentario:
//   # retro:col min_x min_y min_z max_x max_y max_z yaw
// (coordenadas del motor; las escribe el exportador de los modelos).
struct ModelBox {
  Vector3 min{};
  Vector3 max{};
  float yaw = 0.0f;
};

// Un .obj ya leido, solo en memoria (nada de GPU): se puede hacer en un hilo de trabajo y subir
// despues en el principal (ModelResource). Una parte por material, en triangulos sueltos.
struct ObjData {
  struct Part {
    std::string material;
    std::string texture;   // ruta COMPLETA del map_Kd del material (vacia: sin textura)
    Color color = WHITE;   // Kd del material
    std::vector<float> positions; // x y z por vertice
    std::vector<float> texcoords; // u v (v ya invertida, como espera raylib)
    std::vector<float> normals;
  };
  std::vector<Part> parts;
  std::vector<ModelBox> boxes;
};

// Lee un .obj (y su .mtl): v, vt, vn, f (caras de cualquier numero de lados, en abanico;
// v, v/vt, v//vn o v/vt/vn; indices negativos), usemtl, mtllib y las cajas "# retro:col".
// Sin normales en el archivo, cada triangulo usa la suya. Lanza std::runtime_error con el
// archivo y la linea si algo esta mal. No toca OpenGL: sirve en cualquier hilo.
ObjData LoadObjData(const std::string& path);

} // namespace retro
