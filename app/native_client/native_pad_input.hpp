#pragma once

#include <optional>

#include <stellar/engine/input_actions.hpp>
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

// Held dpad directions re-fire after a short delay, then at a fixed
// interval — matching keyboard auto-repeat so a held direction scrolls a
// focus ring instead of stepping once. South/east/start stay discrete
// (activation must not auto-repeat). `note` consumes every raw pad button
// event (a press arms, the release disarms); `update` re-emits the stored
// raw press so the caller re-runs its ownership gate and translation at
// fire time. Devices track separately so two pads cannot disarm each
// other.
class PadNavigationRepeater {
public:
  static constexpr float kInitialDelay=.45f;
  static constexpr float kRepeatInterval=.09f;

  void note(const native_map::InputEvent &event) noexcept {
    using native_map::InputEventType;
    const auto index=direction_index(event.gamepad_button);
    if(index<0)return;
    auto &held=held_[device_slot(event.gamepad_device)][index];
    if(event.type==InputEventType::GamepadPressed){
      held.active=true;
      held.elapsed=0.f;
      held.next=kInitialDelay;
      held.event=event;
    } else if(event.type==InputEventType::GamepadReleased){
      held.active=false;
    }
  }

  void clear() noexcept {
    for(auto &per_device:held_)for(auto &held:per_device)held.active=false;
  }

  // Calls `emit(raw_press)` for each held direction due a repeat.
  template <typename Emit> void update(float dt, Emit &&emit) {
    for(auto &per_device:held_)for(auto &held:per_device)
      if(held.active&&(held.elapsed+=dt)>=held.next){
        // Schedule relative to now — a frame hitch must not burst repeats.
        held.next=held.elapsed+kRepeatInterval;
        emit(held.event);
      }
  }

private:
  struct Held {
    bool active{};
    float elapsed{},next{};
    native_map::InputEvent event{};
  };
  static constexpr int kDeviceSlots=engine::kGamepadDeviceCount+1;
  [[nodiscard]] static constexpr int device_slot(int device) noexcept {
    // Slot 0 holds device-unset events; pads 0..n map to 1..n and an
    // out-of-range device shares the last slot.
    if(device<0)return 0;
    return device<kDeviceSlots-1?device+1:kDeviceSlots-1;
  }
  [[nodiscard]] static constexpr int direction_index(int button) noexcept {
    return button>=11&&button<=14?button-11:-1;
  }
  Held held_[kDeviceSlots][4]{};
};

} // namespace stellar::native_client
