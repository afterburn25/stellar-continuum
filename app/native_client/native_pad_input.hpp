#pragma once

#include <optional>

#include <stellar/engine/native_map_platform.hpp>

namespace stellar::native_client {

// Pad-to-navigation translation for UI surfaces. While a keyboard-focus
// surface owns input, a pad press synthesizes the equivalent key event so
// every focus ring answers the pad without per-surface pad code. Only
// GamepadPressed translates — releases are mapper bookkeeping, axes feed
// live stick values, and unmapped buttons (shoulders, sticks, X/Y) stay
// gameplay-only. Button codes are the SDL_GamepadButton vocabulary.
[[nodiscard]] inline std::optional<native_map::InputEvent> pad_navigation_event(
    const native_map::InputEvent &event) noexcept {
  using native_map::InputEventType;
  if (event.type != InputEventType::GamepadPressed) return std::nullopt;
  native_map::InputEvent nav{};
  switch (event.gamepad_button) {
  case 11: nav.type = InputEventType::KeyPressed; nav.key = 0x40000052u; break; // dpad up
  case 12: nav.type = InputEventType::KeyPressed; nav.key = 0x40000051u; break; // dpad down
  case 13: nav.type = InputEventType::KeyPressed; nav.key = 0x40000050u; break; // dpad left
  case 14: nav.type = InputEventType::KeyPressed; nav.key = 0x4000004fu; break; // dpad right
  case 0:  nav.type = InputEventType::KeyPressed; nav.key = 13u; break;         // south → activate
  case 1:                                                                         // east → back/cancel
  case 6:  nav.type = InputEventType::EscapePressed; break;                       // start → menu/back
  default: return std::nullopt;
  }
  return nav;
}

} // namespace stellar::native_client
