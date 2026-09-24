#include <stellar/engine/input_actions.hpp>

#include <nlohmann/json.hpp>

#include <algorithm>
#include <utility>

namespace stellar::engine {
namespace {

const std::unordered_map<std::string_view, RawInputEvent::Kind> &
kind_names() {
  static const std::unordered_map<std::string_view, RawInputEvent::Kind>
      names{{"KeyPress", RawInputEvent::Kind::KeyPress},
            {"KeyRelease", RawInputEvent::Kind::KeyRelease},
            {"Key", RawInputEvent::Kind::KeyPress},
            {"MouseButton", RawInputEvent::Kind::MouseButton},
            {"MouseMotion", RawInputEvent::Kind::MouseMotion},
            {"MouseWheel", RawInputEvent::Kind::MouseWheel},
            {"GamepadButton", RawInputEvent::Kind::GamepadButton},
            {"GamepadAxis", RawInputEvent::Kind::GamepadAxis}};
  return names;
}

const char *kind_name(RawInputEvent::Kind kind) {
  switch (kind) {
  case RawInputEvent::Kind::KeyPress:
    return "KeyPress";
  case RawInputEvent::Kind::KeyRelease:
    return "KeyRelease";
  case RawInputEvent::Kind::MouseButton:
    return "MouseButton";
  case RawInputEvent::Kind::MouseMotion:
    return "MouseMotion";
  case RawInputEvent::Kind::MouseWheel:
    return "MouseWheel";
  case RawInputEvent::Kind::GamepadButton:
    return "GamepadButton";
  case RawInputEvent::Kind::GamepadAxis:
    return "GamepadAxis";
  }
  return "KeyPress";
}

const char *action_type_name(InputAction::Type type) {
  switch (type) {
  case InputAction::Type::Axis1D:
    return "Axis1D";
  case InputAction::Type::Axis2D:
    return "Axis2D";
  case InputAction::Type::Button:
    return "Button";
  }
  return "Button";
}

} // namespace

bool InputMapper::load_contexts(std::string_view json_document,
                                std::string *error) {
  nlohmann::json doc;
  try {
    doc = nlohmann::json::parse(json_document);
  } catch (const std::exception &ex) {
    if (error != nullptr)
      *error = ex.what();
    return false;
  }
  auto fail = [&](const std::string &message) {
    if (error != nullptr)
      *error = message;
    return false;
  };
  if (!doc.is_object() || !doc.contains("contexts"))
    return fail("input map requires a 'contexts' array");
  for (const auto &context_json : doc.at("contexts")) {
    InputContext context;
    context.name = context_json.value("name", std::string{});
    context.exclusive = context_json.value("exclusive", false);
    if (context.name.empty())
      return fail("context requires a 'name'");
    for (const auto &action_json :
         context_json.value("actions", nlohmann::json::array())) {
      InputAction action;
      action.name = action_json.value("name", std::string{});
      const auto type = action_json.value("type", std::string{"Button"});
      if (type == "Button")
        action.type = InputAction::Type::Button;
      else if (type == "Axis1D")
        action.type = InputAction::Type::Axis1D;
      else if (type == "Axis2D")
        action.type = InputAction::Type::Axis2D;
      else
        return fail("unknown action type '" + type + "'");
      if (action.name.empty())
        return fail("action requires a 'name'");
      for (const auto &binding_json :
           action_json.value("bindings", nlohmann::json::array())) {
        InputBinding binding;
        const auto kind_name =
            binding_json.value("kind", std::string{});
        const auto found = kind_names().find(kind_name);
        if (found == kind_names().end())
          return fail("unknown binding kind '" + kind_name + "'");
        binding.kind = found->second;
        binding.code = binding_json.value("code", 0);
        binding.scale = binding_json.value("scale", 1.0f);
        for (const auto &chord :
             binding_json.value("chord", nlohmann::json::array()))
          binding.chord_keys.push_back(chord.get<int>());
        action.bindings.push_back(binding);
      }
      context.actions.push_back(std::move(action));
    }
    add_context(std::move(context));
  }
  return true;
}

void InputMapper::add_context(InputContext context) {
  contexts_[context.name] = std::move(context);
}

void InputMapper::push_context(std::string_view name) {
  stack_.emplace_back(name);
}
void InputMapper::pop_context() {
  if (!stack_.empty())
    stack_.pop_back();
}
void InputMapper::clear_contexts() { stack_.clear(); }
std::vector<std::string> InputMapper::context_stack() const {
  return stack_;
}

std::vector<std::string> InputMapper::context_names() const {
  std::vector<std::string> names;
  names.reserve(contexts_.size());
  for (const auto &[name, ctx] : contexts_) {
    (void)ctx;
    names.push_back(name);
  }
  return names;
}

bool InputMapper::binding_matches(const InputBinding &binding,
                                  const RawInputEvent &event) const {
  // A release event resolves bindings registered for the matching press
  // kind — actions bind to the button, not to a direction of travel.
  auto effective = event.kind;
  if (effective == RawInputEvent::Kind::KeyRelease)
    effective = RawInputEvent::Kind::KeyPress;
  if (binding.kind != effective)
    return false;
  switch (event.kind) {
  case RawInputEvent::Kind::KeyPress:
  case RawInputEvent::Kind::KeyRelease:
  case RawInputEvent::Kind::MouseButton:
  case RawInputEvent::Kind::GamepadButton:
    return binding.code == event.code;
  case RawInputEvent::Kind::GamepadAxis:
    return binding.code == event.code;
  case RawInputEvent::Kind::MouseMotion:
  case RawInputEvent::Kind::MouseWheel:
    return true; // code-agnostic; scale decides magnitude
  }
  return false;
}

bool InputMapper::chord_satisfied(const InputBinding &binding) const {
  for (const int key : binding.chord_keys) {
    const auto held = held_keys_.find(key);
    if (held == held_keys_.end() || !held->second)
      return false;
  }
  return true;
}

bool InputMapper::feed(const RawInputEvent &event) {
  // Track raw key state for chord evaluation.
  if (event.kind == RawInputEvent::Kind::KeyPress)
    held_keys_[event.code] = true;
  else if (event.kind == RawInputEvent::Kind::KeyRelease)
    held_keys_[event.code] = false;

  for (auto it = stack_.rbegin(); it != stack_.rend(); ++it) {
    const auto found = contexts_.find(*it);
    if (found == contexts_.end())
      continue;
    bool consumed = false;
    for (const auto &action : found->second.actions) {
      for (const auto &binding : action.bindings) {
        if (!binding_matches(binding, event) || !chord_satisfied(binding))
          continue;
        auto &state = states_[action.name];
        const bool button_event =
            event.kind == RawInputEvent::Kind::MouseButton ||
            event.kind == RawInputEvent::Kind::GamepadButton;
        const bool is_press =
            event.kind == RawInputEvent::Kind::KeyPress ||
            (button_event && event.pressed);
        const bool is_release =
            event.kind == RawInputEvent::Kind::KeyRelease ||
            (button_event && !event.pressed);
        if (is_press) {
          if (!state.held)
            state.pressed_this_frame = true;
          state.held = true;
          consumed = true;
          continue;
        }
        if (is_release) {
          if (state.held)
            state.released_this_frame = true;
          state.held = false;
          consumed = true;
          continue;
        }
        switch (event.kind) {
        case RawInputEvent::Kind::GamepadAxis:
          // Sticks only emit on change; keep the latest value so held
          // deflection stays readable between events. axis() folds this
          // live value into the result for GamepadAxis-bound actions.
          gamepad_axes_[event.code] = event.value;
          consumed = true;
          break;
        case RawInputEvent::Kind::MouseMotion:
          if (action.type == InputAction::Type::Axis2D) {
            state.axis += event.x * binding.scale;
            state.axis_y += event.y * binding.scale;
          } else {
            state.axis += event.x * binding.scale;
          }
          consumed = true;
          break;
        case RawInputEvent::Kind::MouseWheel:
          state.axis += event.y * binding.scale;
          consumed = true;
          break;
        default:
          break; // press/release handled above
        }
      }
    }
    if (consumed || found->second.exclusive)
      return consumed || found->second.exclusive;
  }
  return false;
}

void InputMapper::begin_frame() {
  for (auto &[name, state] : states_) {
    (void)name;
    state.pressed_this_frame = false;
    state.released_this_frame = false;
    state.axis = 0.0f;
    state.axis_y = 0.0f;
  }
}

ActionState InputMapper::state(std::string_view action) const {
  const auto found = states_.find(std::string(action));
  return found == states_.end() ? ActionState{} : found->second;
}
bool InputMapper::pressed(std::string_view action) const {
  const auto s = state(action);
  return s.held || s.pressed_this_frame;
}
bool InputMapper::just_pressed(std::string_view action) const {
  return state(action).pressed_this_frame;
}
bool InputMapper::just_released(std::string_view action) const {
  return state(action).released_this_frame;
}
float InputMapper::axis(std::string_view action) const {
  float result = state(action).axis;
  // Gamepad axes hold their last value; add each bound axis's live
  // reading. First context in the stack that defines the action wins.
  for (const auto &name : stack_) {
    const auto ctx = contexts_.find(name);
    if (ctx == contexts_.end())
      continue;
    for (const auto &candidate : ctx->second.actions) {
      if (candidate.name != action)
        continue;
      for (const auto &binding : candidate.bindings)
        if (binding.kind == RawInputEvent::Kind::GamepadAxis) {
          const auto found = gamepad_axes_.find(binding.code);
          if (found != gamepad_axes_.end())
            result += found->second * binding.scale;
        }
      return result;
    }
  }
  return result;
}
float InputMapper::axis_y(std::string_view action) const {
  return state(action).axis_y;
}

std::size_t InputMapper::rebind(std::string_view action,
                                std::vector<InputBinding> bindings) {
  std::size_t rebound = 0;
  for (auto &[name, context] : contexts_)
    for (auto &candidate : context.actions)
      if (candidate.name == action) {
        candidate.bindings = bindings;
        ++rebound;
      }
  // Rebinding invalidates any stale held state from the old bindings.
  states_.erase(std::string(action));
  return rebound;
}

std::vector<InputBinding> InputMapper::bindings(std::string_view action) const {
  for (const auto &name : stack_) {
    const auto ctx = contexts_.find(name);
    if (ctx == contexts_.end())
      continue;
    for (const auto &candidate : ctx->second.actions)
      if (candidate.name == action)
        return candidate.bindings;
  }
  // Not stacked — fall back to any registered context so a rebind UI can
  // inspect actions on inactive pages too.
  for (const auto &[name, ctx] : contexts_)
    for (const auto &candidate : ctx.actions)
      if (candidate.name == action)
        return candidate.bindings;
  return {};
}

std::string InputMapper::save_contexts() const {
  nlohmann::json contexts = nlohmann::json::array();
  std::vector<std::string> names;
  names.reserve(contexts_.size());
  for (const auto &[name, ctx] : contexts_) {
    (void)ctx;
    names.push_back(name);
  }
  std::sort(names.begin(), names.end());
  for (const auto &name : names) {
    const auto &ctx = contexts_.at(name);
    nlohmann::json actions = nlohmann::json::array();
    for (const auto &action : ctx.actions) {
      nlohmann::json bindings = nlohmann::json::array();
      for (const auto &binding : action.bindings) {
        nlohmann::json b{{"kind", kind_name(binding.kind)},
                         {"code", binding.code},
                         {"scale", binding.scale}};
        if (!binding.chord_keys.empty())
          b["chord"] = binding.chord_keys;
        bindings.push_back(std::move(b));
      }
      actions.push_back({{"name", action.name},
                         {"type", action_type_name(action.type)},
                         {"bindings", std::move(bindings)}});
    }
    contexts.push_back({{"name", ctx.name},
                        {"exclusive", ctx.exclusive},
                        {"actions", std::move(actions)}});
  }
  return nlohmann::json{{"contexts", std::move(contexts)}}.dump(2);
}

const InputContext *InputMapper::context(std::string_view name) const {
  const auto it = contexts_.find(std::string(name));
  return it == contexts_.end() ? nullptr : &it->second;
}

std::string key_name(int code) {
  // Printable ASCII (SDL3 keycodes carry it verbatim); letters render
  // uppercase like a physical keycap.
  if (code == 32)
    return "Space";
  if (code >= 'a' && code <= 'z')
    return std::string(1, static_cast<char>(code - 'a' + 'A'));
  if (code > 32 && code < 127)
    return std::string(1, static_cast<char>(code));
  switch (code) {
  case 8: return "Backspace";
  case 9: return "Tab";
  case 13: return "Return";
  case 27: return "Escape";
  case 0x40000039: return "Caps Lock";
  case 0x40000046: return "Print Screen";
  case 0x40000047: return "Scroll Lock";
  case 0x40000048: return "Pause";
  case 0x40000049: return "Insert";
  case 0x4000004a: return "Home";
  case 0x4000004b: return "Page Up";
  case 0x4000004c: return "Delete";
  case 0x4000004d: return "End";
  case 0x4000004e: return "Page Down";
  case 0x4000004f: return "Right";
  case 0x40000050: return "Left";
  case 0x40000051: return "Down";
  case 0x40000052: return "Up";
  case 0x40000053: return "Num Lock";
  case 0x40000054: return "Keypad /";
  case 0x40000055: return "Keypad *";
  case 0x40000056: return "Keypad -";
  case 0x40000057: return "Keypad +";
  case 0x40000058: return "Keypad Enter";
  case 0x40000063: return "Keypad .";
  case 0x400000e0: return "Left Ctrl";
  case 0x400000e1: return "Left Shift";
  case 0x400000e2: return "Left Alt";
  case 0x400000e3: return "Left Gui";
  case 0x400000e4: return "Right Ctrl";
  case 0x400000e5: return "Right Shift";
  case 0x400000e6: return "Right Alt";
  case 0x400000e7: return "Right Gui";
  default: break;
  }
  if (code >= 0x40000059 && code <= 0x40000062)
    return "Keypad " + std::to_string(code - 0x40000059 + 1);
  if (code >= 0x4000003a && code <= 0x40000045)
    return "F" + std::to_string(code - 0x4000003a + 1);
  if (code >= 0x40000068 && code <= 0x40000073)
    return "F" + std::to_string(code - 0x40000068 + 13);
  return "Key " + std::to_string(code);
}

std::string describe_binding(const InputBinding &binding) {
  std::string trigger;
  switch (binding.kind) {
  case RawInputEvent::Kind::KeyPress:
  case RawInputEvent::Kind::KeyRelease:
    trigger = key_name(binding.code);
    break;
  case RawInputEvent::Kind::MouseButton:
    trigger = binding.code == 1   ? "Left Mouse"
              : binding.code == 2 ? "Middle Mouse"
              : binding.code == 3 ? "Right Mouse"
                                  : "Mouse " + std::to_string(binding.code);
    break;
  case RawInputEvent::Kind::MouseWheel:
    trigger = "Wheel";
    break;
  case RawInputEvent::Kind::GamepadButton:
    trigger = "Pad " + std::to_string(binding.code);
    break;
  case RawInputEvent::Kind::GamepadAxis:
    trigger = "Axis " + std::to_string(binding.code);
    break;
  case RawInputEvent::Kind::MouseMotion:
    trigger = "Mouse Motion";
    break;
  }
  for (const int chord : binding.chord_keys)
    trigger = key_name(chord) + "+" + trigger;
  return trigger;
}

std::string describe_bindings(const std::vector<InputBinding> &bindings) {
  if (bindings.empty())
    return "Unbound";
  std::string result;
  for (const auto &binding : bindings) {
    if (!result.empty())
      result += ", ";
    result += describe_binding(binding);
  }
  return result;
}

} // namespace stellar::engine
