#pragma once

namespace stellar::engine {
// Eligibility is supplied by the host at launch. A shortcut alone must never
// enable tooling in an ordinary player process. This is a UX/isolation gate,
// not a security boundary against someone modifying their own executable.
class DeveloperAccess final {
public:
  explicit DeveloperAccess(bool eligible=false) noexcept : eligible_(eligible) {}
  [[nodiscard]] bool eligible() const noexcept {return eligible_;}
  [[nodiscard]] bool active() const noexcept {return active_;}
  bool set_active(bool value) noexcept {
    if(value&&!eligible_)return false;
    active_=value;return true;
  }
private:
  bool eligible_{},active_{};
};
}
