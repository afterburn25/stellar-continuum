#pragma once

// Live smoke for the settings hub's Controls view. Exercises the rebind
// surface end-to-end: category routing, row capture arm, Escape cancel,
// a real keypress rebind through the live InputMapper, the sibling steal
// notice, and persistence. Restore runs through mapper.rebind() — the UI
// has no un-steal path, so the snapshot is pushed back explicitly and the
// persisted file is verified by loading it into a fresh mapper (semantic
// equality, not bytes: save_contexts() canonicalizes and may legitimately
// reorder or emit contexts the original file trimmed).
// Pause-location only: the hub's input mapper is wired after the campaign
// exists, so a startup run would see the static help card, not rows.

#include "native_settings_hub.hpp"

#include <stellar/engine/input_actions.hpp>

#include <algorithm>
#include <cstdint>
#include <fstream>
#include <functional>
#include <iostream>
#include <iterator>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace stellar::native_settings {

template <class Open, class Route, class Capture>
void check_controls_settings(NativeSettingsHub& hub,
                             stellar::engine::InputMapper& mapper,
                             std::string_view button_context,
                             std::string_view axis_context,
                             const std::function<void()>& persist,
                             const std::filesystem::path& controls_path,
                             int width, int height, std::string_view location,
                             Open open_controls, Route route, Capture capture) {
  using namespace stellar::native_map;
  using stellar::engine::InputBinding;
  const auto require = [](bool ok,
                         std::string_view message = "Controls settings check failed.") {
    if (!ok) throw std::runtime_error(std::string(message));
  };
  const auto bytes_at = [](const std::filesystem::path& path)
      -> std::optional<std::string> {
    std::ifstream input(path, std::ios::binary);
    if (!input) return std::nullopt;
    return std::string{std::istreambuf_iterator<char>(input),
                       std::istreambuf_iterator<char>()};
  };
  const auto click = [&](UiRect bounds, float fraction = .5f) {
    const Point point{bounds.x + bounds.width * fraction,
                      bounds.y + bounds.height * .5f};
    route(InputEvent{InputEventType::LeftPressed, point});
    route(InputEvent{InputEventType::LeftReleased, point});
  };
  const auto key = [&](std::uint32_t code) {
    InputEvent event{};
    event.type = InputEventType::KeyPressed;
    event.key = code;
    route(event);
  };
  // InputBinding has no operator== — snapshot comparisons go field-wise.
  const auto same_bindings = [](const std::vector<InputBinding>& a,
                                const std::vector<InputBinding>& b) {
    return a.size() == b.size() &&
           std::ranges::equal(
               a, b, [](const InputBinding& lhs, const InputBinding& rhs) {
                 return lhs.kind == rhs.kind && lhs.code == rhs.code &&
                        lhs.scale == rhs.scale &&
                        lhs.chord_keys == rhs.chord_keys &&
                        lhs.device == rhs.device;
               });
  };

  const auto* buttons = mapper.context(button_context);
  const auto* axes = mapper.context(axis_context);
  require(buttons && axes,
          "Controls view contexts are not registered on the input mapper.");
  // The view lists button-context actions then axis-context axes.
  std::vector<std::string> rows;
  for (const auto& action : buttons->actions)
    if (action.type == stellar::engine::InputAction::Type::Button)
      rows.push_back(action.name);
  for (const auto& action : axes->actions)
    if (action.type == stellar::engine::InputAction::Type::Axis1D)
      rows.push_back(action.name);
  require(rows.size() >= 2,
          "Controls view needs at least two rebindable actions.");

  // Snapshot every action in both contexts — a steal can touch any row.
  std::unordered_map<std::string, std::vector<InputBinding>> snapshot;
  for (const auto* context : {buttons, axes})
    for (const auto& action : context->actions)
      snapshot[action.name] = mapper.bindings(action.name);
  const bool file_preexisted = bytes_at(controls_path).has_value();

  open_controls();
  const auto row_rect = [&](int index) {
    const auto rect = hub.control_row_bounds(index, width, height);
    require(rect.has_value(),
            "Controls view did not open — category routing missed the "
            "rebind list.");
    return *rect;
  };
  const auto first = row_rect(0);
  capture();

  // Escape cancels capture without touching bindings.
  click(first);
  require(hub.capturing(),
          "Clicking a controls row did not arm rebind capture.");
  route(InputEvent{InputEventType::EscapePressed});
  require(!hub.capturing(), "Escape did not cancel rebind capture.");
  require(same_bindings(mapper.bindings(rows[0]), snapshot[rows[0]]),
          "Cancelled rebind capture still changed bindings.");

  // Rebind row 0 to a key nothing else uses — a pure add, no steal.
  std::unordered_set<int> used;
  for (const auto& [name, bound] : snapshot)
    for (const auto& binding : bound)
      if (binding.kind == stellar::engine::RawInputEvent::Kind::KeyPress)
        used.insert(binding.code);
  std::uint32_t probe = 0;
  for (std::uint32_t candidate = 0x40000068u; candidate < 0x40000090u;
       ++candidate)
    if (!used.count(static_cast<int>(candidate))) {
      probe = candidate;
      break;
    }
  require(probe != 0, "No spare keycode available for the rebind probe.");
  click(first);
  require(hub.capturing());
  key(probe);
  require(!hub.capturing(),
          "The probe keypress did not resolve rebind capture.");
  const auto rebound = mapper.bindings(rows[0]);
  require(!rebound.empty() &&
              rebound.front().kind ==
                  stellar::engine::RawInputEvent::Kind::KeyPress &&
              rebound.front().code == static_cast<int>(probe),
          "Rebind did not install the probe as the row's primary binding.");

  // Steal: capture row 0 again and press a key bound to a sibling — the
  // sibling must lose it and the hub must surface the steal notice.
  int victim_row = -1, victim_code = 0;
  for (std::size_t i = 1; i < rows.size() && victim_row < 0; ++i)
    for (const auto& binding : mapper.bindings(rows[i]))
      if (binding.kind == stellar::engine::RawInputEvent::Kind::KeyPress) {
        victim_row = static_cast<int>(i);
        victim_code = binding.code;
        break;
      }
  bool stole = false;
  if (victim_row >= 0) {
    click(first);
    require(hub.capturing());
    key(static_cast<std::uint32_t>(victim_code));
    const auto victim_bound =
        mapper.bindings(rows[static_cast<std::size_t>(victim_row)]);
    const auto still_bound =
        std::ranges::any_of(victim_bound, [&](const InputBinding& binding) {
          return binding.kind ==
                     stellar::engine::RawInputEvent::Kind::KeyPress &&
                 binding.code == victim_code;
        });
    // The steal notice itself is drained into the a11y announcer inside
    // campaign.update the moment the keypress routes — not observable here;
    // the binding removal is the verifiable steal evidence.
    require(!still_bound,
            "Rebinding a bound key did not steal it from the sibling row.");
    stole = true;
  }

  // Overflowing lists scroll: tail rows (the pad-axis entries in the
  // shipped map) carry no hitbox until a wheel tick pages them in —
  // before the list scrolled they were invisible and unclickable.
  bool scrolled = false;
  {
    int on_page = 0;
    while (hub.control_row_bounds(on_page, width, height).has_value())
      ++on_page;
    const int tail = static_cast<int>(rows.size()) - 1;
    if (on_page <= tail) {
      require(!hub.control_row_bounds(tail, width, height).has_value(),
              "Off-page tail row reported a hitbox before scrolling");
      InputEvent wheel{};
      wheel.type = InputEventType::Wheel;
      wheel.position = {first.x + first.width * .5f,
                        first.y + first.height * .5f};
      wheel.wheel_y = -1.f;
      for (int tick = 0; tick < 8 &&
                      !hub.control_row_bounds(tail, width, height).has_value();
           ++tick)
        route(wheel);
      const auto tail_rect = hub.control_row_bounds(tail, width, height);
      require(tail_rect.has_value(),
              "Wheel scrolling did not reach the last rebind row.");
      click(*tail_rect);
      require(hub.capturing(),
              "Scrolled tail row did not arm rebind capture.");
      route(InputEvent{InputEventType::EscapePressed});
      require(!hub.capturing());
      require(same_bindings(mapper.bindings(rows[static_cast<std::size_t>(tail)]),
                            snapshot[rows[static_cast<std::size_t>(tail)]]),
              "Scrolled-row capture cancel changed bindings.");
      scrolled = true;
    }
  }

  // Restore every snapshotted binding through the mapper (un-steal has no
  // UI path), persist, then prove the file round-trips through a fresh
  // mapper — semantic equality per action, not bytes.
  for (const auto& [name, bound] : snapshot) mapper.rebind(name, bound);
  persist();
  const auto saved = bytes_at(controls_path);
  require(saved.has_value() || !file_preexisted,
          "Controls rebind lost the persisted bindings file.");
  if (saved) {
    stellar::engine::InputMapper disk;
    std::string error;
    require(disk.load_contexts(*saved, &error),
            "Persisted galaxy-controls.json did not parse: " + error);
    for (const std::string_view context_name : {button_context, axis_context}) {
      const auto* context = disk.context(context_name);
      require(context != nullptr,
              "Persisted bindings file is missing context " +
                  std::string(context_name));
      for (const auto& action : context->actions)
        require(same_bindings(action.bindings, snapshot[action.name]),
                "Persisted bindings differ from the pre-check snapshot for " +
                    action.name);
    }
  }
  for (const auto& [name, bound] : snapshot)
    require(same_bindings(mapper.bindings(name), bound),
            "Restored bindings do not match the snapshot for " + name);

  // Leave the hub: Escape returns to the categories view, Escape again closes.
  route(InputEvent{InputEventType::EscapePressed});
  require(!hub.control_row_bounds(0, width, height).has_value(),
          "Escape did not leave the controls view.");
  route(InputEvent{InputEventType::EscapePressed});
  require(!hub.visible(), "Settings hub did not close after the controls check.");

  std::cout << "controls_settings_check={\"location\":\"" << location
            << "\",\"opened\":true,\"capture_cancel\":true,\"rebound\":true"
            << ",\"scrolled\":" << (scrolled ? "true" : "false")
            << ",\"stole\":" << (stole ? "true" : "false")
            << ",\"file_preexisted\":" << (file_preexisted ? "true" : "false")
            << ",\"restored\":true}\n";
}

} // namespace stellar::native_settings
