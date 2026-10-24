#pragma once

#include <raylib.h>

namespace editor {

// Cámara libre de editor. Botón derecho: mirar y volar (WASD, Q/E). Rueda: acercar o alejar
// (con el botón derecho pulsado, cambia la velocidad). Botón central: desplazar.
class EditorCamera {
public:
  EditorCamera();
  ~EditorCamera();

  EditorCamera(const EditorCamera&) = delete;
  EditorCamera& operator=(const EditorCamera&) = delete;

  void LookFrom(Vector3 position, Vector3 target);

  void FocusOn(Vector3 target, float distance);

  bool Update(float dt, bool hovered);

  Camera3D Raw() const;
  Vector3 Position() const noexcept { return pos_; }
  Vector3 Forward() const;
  float Speed() const noexcept { return speed_; }

  float fov = 60.0f;

private:
  Vector3 pos_{7.0f, 5.0f, 9.0f};
  float yaw_ = 0.0f;
  float pitch_ = 0.0f;
  float speed_ = 12.0f; // metros por segundo al volar; la rueda (con el derecho) la cambia
  bool looking_ = false;
  int skipFrames_ = 0;
  // Donde estaba el cursor al pulsar el boton derecho: al soltar se devuelve ahi (raylib lo
  // dejaria en el centro de la ventana).
  Vector2 anchor_{0.0f, 0.0f};
};

} // namespace editor
