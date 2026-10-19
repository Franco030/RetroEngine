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

  float fov = 60.0f;

private:
  Vector3 pos_{7.0f, 5.0f, 9.0f};
  float yaw_ = 0.0f;
  float pitch_ = 0.0f;
  float speed_ = 6.0f;
  bool looking_ = false;
  int skipFrames_ = 0;
};

} // namespace editor
