#pragma once

#include <cstddef>
#include <string>
#include <unordered_map>
#include <utility>

namespace retro {

class GameState {
public:
  int Get(const std::string& name) const {
    const auto it = flags_.find(name);
    return it != flags_.end() ? it->second : 0;
  }
  void Set(const std::string& name, int value) { flags_[name] = value; }

  // El mensaje se escribe letra a letra (kLettersPerSecond); sus 'seconds' empiezan a contar
  // cuando ya se ve entero.
  static constexpr float kLettersPerSecond = 40.0f;

  // 'speaker': quien habla (vacio en avisos y notas sin firma).
  void ShowMessage(std::string text, float seconds, std::string speaker = {}) {
    message_ = std::move(text);
    speaker_ = std::move(speaker);
    timer_ = seconds;
    elapsed_ = 0.0f;
    ++messageId_;
  }
  bool HasMessage() const noexcept { return timer_ > 0.0f; }
  void ClearMessage() noexcept { timer_ = 0.0f; }
  const std::string& MessageText() const noexcept { return message_; }
  const std::string& MessageSpeaker() const noexcept { return speaker_; }
  // Cambia con cada mensaje nuevo (aunque el texto se repita).
  unsigned MessageId() const noexcept { return messageId_; }

  // Bytes del mensaje que ya se ven: siempre en el limite de una letra UTF-8 completa.
  std::size_t VisibleBytes() const noexcept {
    std::size_t letters = static_cast<std::size_t>(elapsed_ * kLettersPerSecond);
    std::size_t i = 0;
    while (i < message_.size() && letters > 0) {
      ++i;
      while (i < message_.size() && (static_cast<unsigned char>(message_[i]) & 0xC0) == 0x80)
        ++i; // byte de continuacion: sigue siendo la misma letra
      --letters;
    }
    return i;
  }
  bool FullyShown() const noexcept { return VisibleBytes() >= message_.size(); }
  void RevealAll() noexcept {
    elapsed_ = static_cast<float>(message_.size()) / kLettersPerSecond + 1.0f;
  }

  void Update(float dt) {
    if (timer_ <= 0.0f)
      return;
    if (FullyShown())
      timer_ -= dt;
    else
      elapsed_ += dt;
  }

  void Clear() {
    flags_.clear();
    message_.clear();
    speaker_.clear();
    timer_ = 0.0f;
    elapsed_ = 0.0f;
  }

  const std::unordered_map<std::string, int>& Flags() const noexcept { return flags_; }
  void SetFlags(std::unordered_map<std::string, int> flags) { flags_ = std::move(flags); }

private:
  std::unordered_map<std::string, int> flags_;
  std::string message_;
  std::string speaker_;
  float timer_ = 0.0f;
  float elapsed_ = 0.0f; // tiempo escribiendo el mensaje actual
  unsigned messageId_ = 0;
};

} // namespace retro