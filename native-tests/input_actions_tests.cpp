#include <stellar/engine/input_actions.hpp>

#include <algorithm>
#include <iostream>
#include <string>
#include <vector>

using namespace stellar::engine;

namespace {
int failures = 0;
void check(bool condition, const char *message) {
  if (!condition) {
    std::cerr << "FAIL: " << message << '\n';
    ++failures;
  }
}
RawInputEvent key(RawInputEvent::Kind kind, int code) {
  RawInputEvent event;
  event.kind = kind;
  event.code = code;
  return event;
}
} // namespace

int main() {
  InputMapper mapper;
  std::string error;
  check(mapper.load_contexts(R"({
    "contexts": [
      {"name": "GALAXY", "actions": [
        {"name": "select", "type": "Button",
         "bindings": [{"kind": "MouseButton", "code": 1}]},
        {"name": "pan", "type": "Axis2D",
         "bindings": [{"kind": "MouseMotion", "scale": 1.0}]},
        {"name": "zoom", "type": "Axis1D",
         "bindings": [{"kind": "MouseWheel", "scale": 0.5}]},
        {"name": "thrust", "type": "Axis1D",
         "bindings": [{"kind": "GamepadAxis", "code": 0}]},
        {"name": "boost", "type": "Button",
         "bindings": [{"kind": "GamepadButton", "code": 0}]},
        {"name": "pause", "type": "Button",
         "bindings": [{"kind": "KeyPress", "code": 32}]}
      ]},
      {"name": "UI", "exclusive": true, "actions": [
        {"name": "confirm", "type": "Button",
         "bindings": [{"kind": "KeyPress", "code": 13}]},
        {"name": "quit", "type": "Button",
         "bindings": [{"kind": "KeyPress", "code": 81,
                       "chord": [17]}]}
      ]}
    ]
  })",
                             &error),
        error.c_str());

  mapper.push_context("GALAXY");

  // Button press/release lifecycle.
  mapper.begin_frame();
  RawInputEvent click;
  click.kind = RawInputEvent::Kind::MouseButton;
  click.code = 1;
  check(mapper.feed(click), "click consumed by GALAXY");
  check(mapper.just_pressed("select"), "select pressed edge");
  check(mapper.pressed("select"), "select held");
  mapper.begin_frame();
  check(mapper.pressed("select") && !mapper.just_pressed("select"),
        "held persists without new edge");
  RawInputEvent release;
  release.kind = RawInputEvent::Kind::MouseButton;
  release.code = 1;
  release.pressed = false;
  mapper.feed(release);
  check(mapper.just_released("select"), "release edge fires");

  // Axis accumulation.
  mapper.begin_frame();
  RawInputEvent wheel;
  wheel.kind = RawInputEvent::Kind::MouseWheel;
  wheel.y = 3;
  mapper.feed(wheel);
  check(mapper.axis("zoom") == 1.5f, "wheel scale applied");
  RawInputEvent motion;
  motion.kind = RawInputEvent::Kind::MouseMotion;
  motion.x = 10;
  motion.y = -4;
  mapper.feed(motion);
  check(mapper.axis("pan") == 10.0f && mapper.axis_y("pan") == -4.0f,
        "2D motion accumulates");
  mapper.begin_frame();
  check(mapper.axis("zoom") == 0.0f, "axes reset per frame");

  // Gamepad buttons behave like keys; axes hold their last value across
  // frames because devices only emit change events.
  mapper.begin_frame();
  RawInputEvent pad_press;
  pad_press.kind = RawInputEvent::Kind::GamepadButton;
  pad_press.code = 0;
  check(mapper.feed(pad_press), "pad button consumed");
  check(mapper.just_pressed("boost") && mapper.pressed("boost"),
        "pad button edge + hold");
  pad_press.pressed = false;
  mapper.feed(pad_press);
  check(mapper.just_released("boost"), "pad button release edge");
  RawInputEvent stick;
  stick.kind = RawInputEvent::Kind::GamepadAxis;
  stick.code = 0;
  stick.value = 0.75f;
  mapper.feed(stick);
  check(mapper.axis("thrust") == 0.75f, "pad axis reads value");
  mapper.begin_frame();
  check(mapper.axis("thrust") == 0.75f,
        "pad axis persists between events");
  stick.value = -0.25f;
  mapper.feed(stick);
  check(mapper.axis("thrust") == -0.25f, "pad axis updates on change");
  mapper.begin_frame();
  check(mapper.axis("thrust") == -0.25f, "pad axis keeps new value");

  // Context stack: exclusive UI swallows keys meant for GALAXY.
  mapper.push_context("UI");
  mapper.begin_frame();
  check(mapper.feed(key(RawInputEvent::Kind::KeyPress, 32)),
        "space consumed somewhere");
  check(!mapper.pressed("pause"), "exclusive UI blocks lower context");
  check(mapper.feed(key(RawInputEvent::Kind::KeyPress, 13)),
        "confirm consumed");
  check(mapper.just_pressed("confirm"), "confirm edge");
  mapper.pop_context();
  mapper.begin_frame();
  mapper.feed(key(RawInputEvent::Kind::KeyPress, 32));
  check(mapper.just_pressed("pause"), "GALAXY resumes after pop");

  // Chord: quit needs Ctrl (17) held.
  mapper.push_context("UI");
  mapper.begin_frame();
  mapper.feed(key(RawInputEvent::Kind::KeyPress, 81));
  check(!mapper.just_pressed("quit"), "chord blocks unmodified key");
  mapper.feed(key(RawInputEvent::Kind::KeyPress, 17)); // hold Ctrl
  mapper.feed(key(RawInputEvent::Kind::KeyRelease, 81));
  mapper.feed(key(RawInputEvent::Kind::KeyPress, 81));
  check(mapper.just_pressed("quit"), "chord satisfied fires quit");

  // Rebinding.
  InputBinding rebound;
  rebound.kind = RawInputEvent::Kind::KeyPress;
  rebound.code = 90;
  check(mapper.rebind("confirm", {rebound}) == 1, "rebind reports count");
  mapper.begin_frame();
  mapper.feed(key(RawInputEvent::Kind::KeyPress, 13));
  check(!mapper.just_pressed("confirm"), "old binding gone");
  mapper.feed(key(RawInputEvent::Kind::KeyPress, 90));
  check(mapper.just_pressed("confirm"), "new binding works");

  // Registered-context enumeration (activating a freshly loaded map).
  const auto names = mapper.context_names();
  check(names.size() == 2 &&
            std::find(names.begin(), names.end(), "GALAXY") !=
                names.end() &&
            std::find(names.begin(), names.end(), "UI") != names.end(),
        "context_names lists registered contexts");

  // bindings(): a rebind UI reads what an action is bound to — the first
  // stacked context wins, and unstacked contexts are still inspectable.
  check(mapper.bindings("confirm").size() == 1 &&
            mapper.bindings("confirm")[0].code == 90,
        "bindings reports rebound key");
  mapper.pop_context(); // UI unstacked — still readable
  check(mapper.bindings("quit").size() == 1 &&
            mapper.bindings("quit")[0].code == 81 &&
            mapper.bindings("quit")[0].chord_keys.size() == 1,
        "unstacked context bindings inspectable");

  // save_contexts() round-trips the rebound map through load_contexts.
  const auto saved = mapper.save_contexts();
  InputMapper reloaded;
  check(reloaded.load_contexts(saved, &error), error.c_str());
  reloaded.push_context("UI");
  reloaded.begin_frame();
  reloaded.feed(key(RawInputEvent::Kind::KeyPress, 90));
  check(reloaded.just_pressed("confirm"),
        "rebound binding survives save/load");
  check(reloaded.bindings("quit").size() == 1 &&
            reloaded.bindings("quit")[0].chord_keys.size() == 1 &&
            reloaded.bindings("quit")[0].chord_keys[0] == 17,
        "chord survives round-trip");
  reloaded.push_context("GALAXY");
  reloaded.begin_frame();
  RawInputEvent stick2;
  stick2.kind = RawInputEvent::Kind::GamepadAxis;
  stick2.code = 0;
  stick2.value = 0.5f;
  reloaded.feed(stick2);
  check(reloaded.axis("thrust") == 0.5f, "axis binding survives round-trip");
  check(reloaded.context_names().size() == 2,
        "context set survives round-trip");

  // context(name): rebind UIs enumerate a context's action list directly.
  const auto *galaxy = mapper.context("GALAXY");
  check(galaxy != nullptr && !galaxy->actions.empty(),
        "context(GALAXY) returns the registered context");
  check(mapper.context("MISSING") == nullptr, "missing context is nullptr");
  check(mapper.context("GALAXY")->name == "GALAXY",
        "context accessor returns the named context");

  // key_name/describe_binding: display names a rebind UI renders.
  check(key_name(32) == "Space" && key_name('a') == "A" && key_name('4') == "4",
        "key_name covers space and printables");
  check(key_name(0x4000003f) == "F6" && key_name(0x4000004f) == "Right" &&
            key_name(0x400000e0) == "Left Ctrl",
        "key_name covers function keys, arrows and modifiers");
  check(key_name(999999) == "Key 999999", "unknown keycode falls back");
  check(describe_binding({RawInputEvent::Kind::KeyPress, 32}) == "Space",
        "describe_binding names a keypress");
  check(describe_binding({RawInputEvent::Kind::MouseButton, 3}) == "Right Mouse",
        "describe_binding names a mouse button");
  check(describe_bindings(mapper.bindings("quit")) == "Key 17+Q",
        "describe_bindings renders chord then trigger");
  check(describe_bindings({}) == "Unbound", "empty bindings render Unbound");

  // Multi-pad device pinning: a binding with "device" answers only that
  // pad slot; unpinned bindings answer any pad; device-unset events
  // (synthetic/replayed input) are wildcards that still match pins.
  {
    InputMapper devices;
    check(devices.load_contexts(R"({"contexts":[{"name":"PADS","actions":[
        {"name":"pad0_fire","type":"Button","bindings":[{"kind":"GamepadButton","code":1,"device":0}]},
        {"name":"any_fire","type":"Button","bindings":[{"kind":"GamepadButton","code":2}]},
        {"name":"pad1_stick","type":"Axis1D","bindings":[{"kind":"GamepadAxis","code":0,"device":1}]},
        {"name":"any_stick","type":"Axis1D","bindings":[{"kind":"GamepadAxis","code":1}]}]}]})",
                             &error),
          error.c_str());
    devices.push_context("PADS");
    devices.begin_frame();
    RawInputEvent button{};
    button.kind = RawInputEvent::Kind::GamepadButton;
    button.code = 1;
    button.device = 1;
    devices.feed(button);
    check(!devices.just_pressed("pad0_fire"),
          "pinned binding ignored a different pad");
    button.device = 0;
    devices.feed(button);
    check(devices.just_pressed("pad0_fire"), "pinned binding fired on its pad");
    RawInputEvent any_button{};
    any_button.kind = RawInputEvent::Kind::GamepadButton;
    any_button.code = 2;
    any_button.device = 3;
    devices.feed(any_button);
    check(devices.just_pressed("any_fire"),
          "unpinned binding fired on any pad");
    // A device-unset (replayed/synthetic) event still matches pinned
    // bindings.
    button.device = -1;
    button.pressed = false;
    devices.feed(button);
    button.pressed = true;
    devices.begin_frame();
    devices.feed(button);
    check(devices.just_pressed("pad0_fire"),
          "device-unset event matched a pinned binding");
    // Per-device axis state: a pinned axis reads only its pad plus
    // wildcard events; an unpinned axis sums every pad.
    RawInputEvent axis{};
    axis.kind = RawInputEvent::Kind::GamepadAxis;
    axis.code = 0;
    axis.device = 0;
    axis.value = 0.4f;
    devices.feed(axis);
    check(devices.axis("pad1_stick") == 0.0f,
          "pinned axis ignored another pad");
    axis.device = 1;
    axis.value = 0.6f;
    devices.feed(axis);
    check(devices.axis("pad1_stick") == 0.6f, "pinned axis read its pad");
    axis.device = -1;
    axis.value = 0.2f;
    devices.feed(axis);
    check(devices.axis("pad1_stick") == 0.8f,
          "wildcard axis event fed the pinned binding");
    axis.code = 1;
    axis.device = 0;
    axis.value = 0.25f;
    devices.feed(axis);
    axis.device = 2;
    axis.value = 0.5f;
    devices.feed(axis);
    check(devices.axis("any_stick") == 0.75f,
          "unpinned axis summed every pad");
    // JSON round-trip preserves the device pin; unpinned bindings stay
    // absent from the document.
    const auto document = devices.save_contexts();
    InputMapper reloaded_devices;
    check(reloaded_devices.load_contexts(document, &error), error.c_str());
    check(reloaded_devices.bindings("pad0_fire")[0].device == 0 &&
              reloaded_devices.bindings("pad1_stick")[0].device == 1 &&
              reloaded_devices.bindings("any_fire")[0].device < 0,
          "device pin survived the save/load round-trip");
  }

  if (failures == 0)
    std::cout << "InputMapper tests passed\n";
  return failures == 0 ? 0 : 1;
}
