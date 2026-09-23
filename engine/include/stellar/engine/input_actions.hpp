#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace stellar::engine {

// Engine-level input action system. The platform layer (SDL adapter) feeds
// normalized RawInputEvents; the mapper resolves them through the active
// InputContext stack into named gameplay actions. Game code queries action
// state — it never sees raw device codes.
//
// Contexts stack: "UI" can sit on top of "GALAXY"; feed() walks the stack
// top-down and stops when a context consumes the input. Bindings are
// data-driven (JSON) and rebindable at runtime.

struct RawInputEvent {
  enum class Kind {
    KeyPress,        // code = platform keycode
    KeyRelease,
    MouseButton,     // code = button id; value unused
    MouseMotion,     // code unused; x/y = delta
    MouseWheel,      // x/y = wheel delta
    GamepadButton,   // code = button id
    GamepadAxis,     // code = axis id; value = -1..1
  };
  Kind kind{};
  int code{};
  float value{};     // axis value or wheel scalar
  float x{}, y{};    // motion deltas / wheel vector
  // For MouseButton/GamepadButton: true = pressed, false = released.
  // KeyPress/KeyRelease encode direction in the kind itself.
  bool pressed{true};
};

struct InputBinding {
  RawInputEvent::Kind kind{};
  int code{};
  float scale{1.0f};
  // Optional chord: all chord keys must be held for the binding to fire.
  std::vector<int> chord_keys;
};

struct InputAction {
  enum class Type { Button, Axis1D, Axis2D };
  std::string name;
  Type type{Type::Button};
  std::vector<InputBinding> bindings;
};

struct InputContext {
  std::string name;
  std::vector<InputAction> actions;
  // When false, unhandled inputs fall through to the context below.
  bool exclusive{};
};

struct ActionState {
  bool held{};
  bool pressed_this_frame{};
  bool released_this_frame{};
  float axis{}, axis_y{};
};

class InputMapper {
public:
  // Registers a context. Later push_context/set_active decides which stack
  // entry handles input. Loading JSON of the form
  // {"contexts":[{"name":"GALAXY","exclusive":false,
  //   "actions":[{"name":"zoom","type":"Axis1D",
  //     "bindings":[{"kind":"MouseWheel","scale":0.1}]}]}]}
  bool load_contexts(std::string_view json_document,
                     std::string *error = nullptr);
  void add_context(InputContext context);

  void push_context(std::string_view name);
  void pop_context();
  void clear_contexts();
  std::vector<std::string> context_stack() const;
  // All registered context names (unordered) — needed to activate a
  // freshly loaded map file.
  std::vector<std::string> context_names() const;

  // Feeds one raw event. Returns true when a context consumed it.
  bool feed(const RawInputEvent &event);

  // Clears pressed/released edges. Call once per frame before feeding.
  void begin_frame();

  ActionState state(std::string_view action) const;
  bool pressed(std::string_view action) const;   // held or pressed this frame
  bool just_pressed(std::string_view action) const;
  bool just_released(std::string_view action) const;
  float axis(std::string_view action) const;
  float axis_y(std::string_view action) const;

  // Rebinding: replaces all bindings for `action` in every context where it
  // exists. Returns the number of actions rebound.
  std::size_t rebind(std::string_view action,
                     std::vector<InputBinding> bindings);

private:
  bool binding_matches(const InputBinding &binding,
                       const RawInputEvent &event) const;
  bool chord_satisfied(const InputBinding &binding) const;

  std::unordered_map<std::string, InputContext> contexts_;
  std::vector<std::string> stack_;
  std::unordered_map<std::string, ActionState> states_;
  std::unordered_map<int, bool> held_keys_; // for chord evaluation
};

} // namespace stellar::engine
