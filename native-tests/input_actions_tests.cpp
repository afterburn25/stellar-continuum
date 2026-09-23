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

  if (failures == 0)
    std::cout << "InputMapper tests passed\n";
  return failures == 0 ? 0 : 1;
}
