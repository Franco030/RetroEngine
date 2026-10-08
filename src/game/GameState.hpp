#pragma once

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

  void ShowMessage(std::string text, float seconds) {
    message_ = std::move(text);
    timer_ = seconds;
  }
  bool HasMessage() const noexcept { return timer_ > 0.0f; }
  const std::string& MessageText() const noexcept { return message_; }

  void Update(float dt) {
    if (timer_ > 0.0f)
      timer_ -= dt;
  }

  void Clear() {
    flags_.clear();
    message_.clear();
    timer_ = 0.0f;
  }

private:
  std::unordered_map<std::string, int> flags_;
  std::string message_;
  float timer_ = 0.0f;
};

} // namespace retro