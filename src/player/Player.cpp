#include "player/Player.hpp"

#include "graphics/RetroCamera.hpp"

#include <algorithm>
#include <cmath>

namespace retro {

namespace {

constexpr float kMaxDt = 0.05f;
constexpr float kMaxSubstep = 1.0f / 120.0f; // evita atravesar paredes
constexpr float kMaxPitch = 85.0f;
constexpr float kGroundAccel = 60.0f;
constexpr float kAirAccel = 12.0f;

} // namespace

Player::Player(PlayerSettings settings) : cfg_(settings) {}

Player::~Player() {
  if (active_)
    EnableCursor();
}

void Player::Spawn(Vector3 feet, float yawDegrees) {
  feet_ = feet;
  vel_ = {0.0f, 0.0f};
  velY_ = 0.0f;
  yaw_ = yawDegrees;
  pitch_ = 0.0f;
  grounded_ = false;
}

void Player::SetActive(bool active) {
  if (active == active_)
    return;

  active_ = active;
  if (active_) {
    DisableCursor();
    skipMouseFrames_ = 2;
  } else {
    EnableCursor();
  }
}

void Player::Look() {
  const Vector2 d = GetMouseDelta();
  if (skipMouseFrames_ > 0) {
    --skipMouseFrames_;
    return;
  }
  yaw_ -= d.x * cfg_.mouseSensitivity;
  pitch_ = std::clamp(pitch_ - d.y * cfg_.mouseSensitivity, -kMaxPitch, kMaxPitch);
}

void Player::Update(float dt, std::span<const WorldBox> boxes) {
  dt = std::min(dt, kMaxDt);
  if (dt <= 0.0f)
    return;

  Vector2 wish{0.0f, 0.0f};
  bool run = false;
  bool jump = false;

  if (active_) {
    Look();

    const float fwd = static_cast<float>(IsKeyDown(KEY_W)) - static_cast<float>(IsKeyDown(KEY_S));
    const float side = static_cast<float>(IsKeyDown(KEY_D)) - static_cast<float>(IsKeyDown(KEY_A));

    const float yaw = yaw_ * DEG2RAD;
    const Vector2 f{-std::sin(yaw), -std::cos(yaw)};
    const Vector2 r{std::cos(yaw), -std::sin(yaw)};

    wish = {f.x * fwd + r.x * side, f.y * fwd + r.y * side};
    const float len = std::hypot(wish.x, wish.y);
    if (len > 1.0f)
      wish = {wish.x / len, wish.y / len}; // diagonal sin ir más rápido

    run = IsKeyDown(KEY_LEFT_SHIFT);
    jump = IsKeyPressed(KEY_SPACE);
  }

  // Aceleración hacia la velocidad deseada (más control en el suelo).
  const float speed = run ? cfg_.runSpeed : cfg_.walkSpeed;
  Vector2 diff{wish.x * speed - vel_.x, wish.y * speed - vel_.y};
  const float maxDelta = (grounded_ ? kGroundAccel : kAirAccel) * dt;
  const float diffLen = std::hypot(diff.x, diff.y);
  if (diffLen > maxDelta)
    diff = {diff.x * maxDelta / diffLen, diff.y * maxDelta / diffLen};
  vel_ = {vel_.x + diff.x, vel_.y + diff.y};

  if (jump && grounded_) {
    velY_ = std::sqrt(2.0f * cfg_.gravity * cfg_.jumpHeight);
    grounded_ = false;
  }

  const int steps = std::max(1, static_cast<int>(std::ceil(dt / kMaxSubstep)));
  const float h = dt / static_cast<float>(steps);
  for (int i = 0; i < steps; ++i)
    Step(h, boxes);
}

void Player::Step(float h, std::span<const WorldBox> boxes) {
  // --- Horizontal: mover y sacar del interior de las cajas ---
  Vector2 p{feet_.x + vel_.x * h, feet_.z + vel_.y * h};

  for (int iter = 0; iter < 3; ++iter) { // varias pasadas para resolver esquinas
    bool any = false;
    for (const WorldBox& b : boxes) {
      // Una caja que se puede pisar (top <= pies + step) o que está por
      // encima de la cabeza no bloquea el movimiento.
      if (feet_.y + cfg_.stepHeight >= b.top)
        continue;
      if (feet_.y + cfg_.height <= b.bottom)
        continue;
      any |= PushOutCircle(b, p, cfg_.radius);
    }
    if (!any)
      break;
  }
  feet_.x = p.x;
  feet_.z = p.y;

  // --- Vertical: gravedad y suelo ---
  const float prevY = feet_.y;
  velY_ -= cfg_.gravity * h;
  feet_.y += velY_ * h;

  float ground = 0.0f; // el plano Y=0 es el suelo base
  for (const WorldBox& b : boxes) {
    if (b.top <= prevY + cfg_.stepHeight && b.top > ground && OverlapsCircle(b, p, cfg_.radius)) {
      ground = b.top;
    }
  }

  if (feet_.y <= ground) {
    feet_.y = ground;
    velY_ = 0.0f;
    grounded_ = true;
  } else {
    grounded_ = false;
  }
}

void Player::ApplyToCamera(RetroCamera& camera) const { camera.SetView(Eye(), yaw_, pitch_); }

} // namespace retro