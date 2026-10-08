#pragma once

#include "physics/Collision.hpp"

#include <raylib.h>

#include <span>

namespace retro {

class RetroCamera;

struct PlayerSettings {
  float radius = 0.3f; // cilindro de colision
  float height = 1.7f;
  float eyeHeight = 1.55f;
  float stepHeight = 0.35f; // desniveles que se suben sin saltar
  float walkSpeed = 3.5f;
  float runSpeed = 6.0f;
  float jumpHeight = 0.9f;
  float gravity = 20.0f;
  float mouseSensitivity = 0.12f; // grados por pixel
};

// Jugador en primera persona: cilindro vertical (sin cuerpo visible) que
// colisiona con el suelo (plano Y=0) y con las WorldBox de la escena.
class Player {
public:
  explicit Player(PlayerSettings settings = {});
  ~Player();

  Player(const Player&) = delete;
  Player& operator=(const Player&) = delete;

  const PlayerSettings& Settings() const noexcept { return cfg_; }

  void Spawn(Vector3 feet, float yawDegrees);

  void SetActive(bool active);
  bool Active() const noexcept { return active_; }

  void Update(float dt, std::span<const WorldBox> boxes);
  void ApplyToCamera(RetroCamera& camera) const;

  Vector3 Feet() const noexcept { return feet_; }
  Vector3 Eye() const noexcept { return {feet_.x, feet_.y + cfg_.eyeHeight, feet_.z}; }

  bool Grounded() const noexcept { return grounded_; }
  float Yaw() const noexcept { return yaw_; }

private:
  void Look();
  void Step(float h, std::span<const WorldBox> boxes);

  PlayerSettings cfg_;
  Vector3 feet_{0.0f, 0.0f, 0.0f};
  Vector2 vel_{0.0f, 0.0f}; // velocidad horizontal (x, z)
  float velY_ = 0.0f;
  float yaw_ = 0.0f; // grados
  float pitch_ = 0.0f;
  bool grounded_ = false;
  bool active_ = false;
  int skipMouseFrames_ = 0;
};

} // namespace retro