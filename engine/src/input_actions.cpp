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
          state.axis = event.value * binding.scale;
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
  return state(action).axis;
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

} // namespace stellar::engine
