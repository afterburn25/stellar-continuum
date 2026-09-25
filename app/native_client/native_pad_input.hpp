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

// Stick-driven UI navigation: a left-stick deflection past the engage
// threshold behaves like the matching dpad direction — it emits a
// synthesized GamepadPressed (so callers run their ownership gate and
// pad_navigation_event translation exactly once) and feeds the repeater
// so a held stick auto-repeats like a held dpad direction. Releasing
// below the lower release threshold emits the synthetic release. The
// hysteresis band keeps a stick resting near the edge from toggling.
// Axes feed continuous values, so transitions — not magnitudes — drive
// the state machine.
class PadStickNavigator {
public:
  static constexpr float kEngageThreshold=.5f;
  static constexpr float kReleaseThreshold=.25f;

  // Returns the synthesized press once when the deflection engages a new
  // direction AND `arm` is set; disarm bookkeeping is always forwarded to
  // `repeater` so a direction held across an ownership change still
  // clears. The caller passes its input-ownership verdict as `arm` — a
  // camera-bound axis keeps its gameplay meaning (bindings win) and
  // neither translates nor arms the repeater.
  [[nodiscard]] std::optional<native_map::InputEvent> note(
      const native_map::InputEvent &event, PadNavigationRepeater &repeater,
      bool arm) noexcept {
    using native_map::InputEventType;
    if(event.type!=InputEventType::GamepadAxis)return std::nullopt;
    const auto axis=axis_index(event.gamepad_axis);
    if(axis<0)return std::nullopt;
    auto &state=held_[device_slot(event.gamepad_device)][axis];
    // Directions: stick left/right on axis 0 map to dpad 13/14, up/down on
    // axis 1 map to 11/12 (SDL +Y is down).
    const int direction=event.gamepad_axis_value>=kEngageThreshold?1:
        event.gamepad_axis_value<=-kEngageThreshold?-1:
        event.gamepad_axis_value<=kReleaseThreshold&&
        event.gamepad_axis_value>=-kReleaseThreshold?0:state.direction;
    if(direction==state.direction)return std::nullopt;
    std::optional<native_map::InputEvent> engaged;
    if(state.direction!=0)
      repeater.note(synthetic(event,InputEventType::GamepadReleased,
                              direction_button(axis,state.direction)));
    state.direction=0;
    if(direction!=0){
      state.direction=direction;
      const auto press=synthetic(event,InputEventType::GamepadPressed,
                                 direction_button(axis,direction));
      if(arm){
        repeater.note(press);
        engaged=press;
      }
    }
    return engaged;
  }

  void clear(PadNavigationRepeater &repeater) noexcept {
    for(int device=0;device<kDeviceSlots;++device)
      for(int axis=0;axis<2;++axis){
        auto &state=held_[device][axis];
        if(state.direction!=0){
          native_map::InputEvent release{};
          release.type=native_map::InputEventType::GamepadReleased;
          release.gamepad_button=direction_button(axis,state.direction);
          release.gamepad_device=device>0?static_cast<std::uint8_t>(device-1):0;
          repeater.note(release);
          state.direction=0;
        }
      }
  }

private:
  struct AxisState { int direction{}; };
  static constexpr int kDeviceSlots=engine::kGamepadDeviceCount+1;
  [[nodiscard]] static constexpr int device_slot(int device) noexcept {
    if(device<0)return 0;
    return device<kDeviceSlots-1?device+1:kDeviceSlots-1;
  }
  // Only the left stick navigates — the right stick stays on camera
  // bindings (map_zoom lives on axis 3).
  [[nodiscard]] static constexpr int axis_index(int axis) noexcept {
    return axis>=0&&axis<=1?axis:-1;
  }
  [[nodiscard]] static constexpr std::uint8_t direction_button(
      int axis,int direction) noexcept {
    return static_cast<std::uint8_t>(
        axis==0?(direction<0?13:14):(direction<0?11:12));
  }
  [[nodiscard]] static native_map::InputEvent synthetic(
      const native_map::InputEvent &source,native_map::InputEventType type,
      std::uint8_t button) noexcept {
    native_map::InputEvent event{};
    event.type=type;
    event.gamepad_button=button;
    event.gamepad_device=source.gamepad_device;
    return event;
  }
  AxisState held_[kDeviceSlots][2]{};
};

} // namespace stellar::native_client
