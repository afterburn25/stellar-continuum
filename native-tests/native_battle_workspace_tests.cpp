#include "native_battle_workspace.hpp"

#include <cmath>
#include <iostream>
#include <stdexcept>
#include <string_view>

namespace {
using namespace stellar::core;
using namespace stellar::native_battle_ui;
using namespace stellar::native_map;

void require(bool value, std::string_view message) {
  if (!value) throw std::runtime_error(std::string(message));
}

Point center(UiRect value) {
  return {value.x + value.width * .5f, value.y + value.height * .5f};
}

InputEvent press(InputEventType type, Point at) {
  InputEvent event{};
  event.type = type;
  event.position = at;
  return event;
}

MassiveObservedFormation formation(std::int64_t id, int civilization,
                                   MassivePoint at,
                                   std::string name = "Formation",
                                   int low = 40, int high = 60) {
  MassiveObservedFormation value;
  value.formation_id = id;
  value.civilization_id = civilization;
  value.display_name = std::move(name);
  value.position = at;
  value.velocity = {8.f, 0.f};
  value.shape = MassiveFormationShape::Wedge;
  value.ship_count_low = low;
  value.ship_count_high = high;
  value.strength_low = 120.f;
  value.strength_high = 240.f;
  return value;
}

MassiveCombatSnapshot snapshot() {
  MassiveCombatSnapshot value;
  value.battle_id = {1, 2, 3, 4};
  value.tick = 420;
  value.simulated_seconds = 42.;
  value.exact_own_ships = 100;
  value.formations.push_back(
      formation(11, 1, {-60.f, 0.f}, "Vanguard Fleet", 30, 30));
  value.formations.back().is_exact = true;
  value.formations.push_back(
      formation(12, 1, {-20.f, 40.f}, "Screen Wing", 20, 20));
  value.formations.back().is_exact = true;
  value.formations.push_back(
      formation(77, 9, {60.f, 10.f}, "Unidentified contact", 40, 90));
  value.events.push_back({.sequence = 1,
                          .tick = 400,
                          .type = MassiveCombatEventType::BeamVolley,
                          .actor_civilization_id = 1,
                          .actor_formation_id = 11,
                          .target_formation_id = 77,
                          .magnitude = 12,
                          .position = MassivePoint{10.f, 5.f},
                          .message = "Volley",
                          .details_known = true});
  value.active_missile_salvos.push_back(
      {.salvo_id = 5,
       .source_position = MassivePoint{-60.f, 0.f},
       .target_position = MassivePoint{60.f, 10.f},
       .current_position = MassivePoint{0.f, 4.f},
       .remaining_seconds = 8.f,
       .progress_01 = .5f,
       .count_low = 6,
       .count_high = 12,
       .incoming_to_own = false});
  return value;
}

void responsive_layout() {
  for (const auto [width, height] :
       {std::pair{640, 360}, {1280, 720}, {1920, 1080}, {2560, 1440}}) {
    const auto layout = BattleWorkspaceLayout::for_viewport(width, height);
    require(layout.surface.contains(center(layout.play)), "play escaped");
    require(layout.surface.contains(center(layout.speed)), "speed escaped");
    require(layout.surface.contains(center(layout.fit)), "fit escaped");
    require(layout.surface.contains(center(layout.menu)), "menu escaped");
    require(layout.surface.contains(center(layout.orders)),
            "orders escaped");
    require(layout.surface.contains(center(layout.status)),
            "status escaped");
    for (const auto &button : layout.order_buttons)
      require(layout.orders.contains(center(button)) ||
                  button.y < layout.orders.y + layout.orders.height,
              "order button drifted outside orders column");
  }
}

void open_fit_and_render() {
  NativeBattleWorkspace workspace;
  require(!workspace.visible(), "workspace visible before open");
  workspace.open(snapshot(), 1, 1280, 720);
  require(workspace.visible(), "workspace not visible after open");
  require(workspace.snapshot() && workspace.snapshot()->formations.size() == 3,
          "snapshot not retained");
  DrawList draw;
  workspace.render(draw, 1280, 720);
  require(workspace.rendered_tokens() > 0, "no formation tokens rendered");
  require(workspace.rendered_tokens() <= 4096,
          "token pool exceeded reference cap");
  require(!draw.overlay.empty(), "no overlay commands emitted");
  require(!draw.circles.empty(), "no token circles emitted");
}

void selection_rules() {
  constexpr int width = 1280, height = 720;
  NativeBattleWorkspace workspace;
  workspace.open(snapshot(), 1, width, height);
  const auto own = workspace.project({-60.f, 0.f}, width, height);
  const auto hostile = workspace.project({60.f, 10.f}, width, height);

  auto command = workspace.handle(press(InputEventType::LeftPressed, own),
                                  width, height);
  require(command.captured, "press not captured");
  command = workspace.handle(press(InputEventType::LeftReleased, own),
                             width, height);
  require(workspace.selection().contains(11), "own formation not selected");

  command = workspace.handle(press(InputEventType::LeftPressed, hostile),
                             width, height);
  command = workspace.handle(press(InputEventType::LeftReleased, hostile),
                             width, height);
  require(workspace.selection().empty(),
          "foreign formation must not enter the orderable selection");

  // Box selection captures only own formations.
  command = workspace.handle(press(InputEventType::LeftPressed, {40.f, 60.f}),
                             width, height);
  InputEvent drag{};
  drag.type = InputEventType::PointerMove;
  drag.position = {700.f, 500.f};
  drag.delta = {660.f, 440.f};
  command = workspace.handle(drag, width, height);
  command = workspace.handle(press(InputEventType::LeftReleased, {700.f, 500.f}),
                             width, height);
  require(workspace.selection().contains(11) &&
              workspace.selection().contains(12) &&
              !workspace.selection().contains(77),
          "box selection must cover own formations only");
}

void order_commands() {
  constexpr int width = 1280, height = 720;
  const auto layout = BattleWorkspaceLayout::for_viewport(width, height);
  NativeBattleWorkspace workspace;
  workspace.open(snapshot(), 1, width, height);

  // Order button with no selection: warning, no command.
  auto command = workspace.handle(
      press(InputEventType::LeftPressed, center(layout.order_buttons[0])),
      width, height);
  require(command.kind == BattleWorkspaceCommandKind::None,
          "order issued without selection");

  const auto own = workspace.project({-60.f, 0.f}, width, height);
  (void)workspace.handle(press(InputEventType::LeftPressed, own), width,
                         height);
  (void)workspace.handle(press(InputEventType::LeftReleased, own), width,
                         height);
  require(workspace.selection().contains(11), "selection failed");

  // Non-targeted order button emits an order for the selected formation.
  command = workspace.handle(
      press(InputEventType::LeftPressed, center(layout.order_buttons[0])),
      width, height);
  require(command.kind == BattleWorkspaceCommandKind::IssueOrder,
          "non-targeted order missing");
  require(command.order.formation_id == 11 &&
              command.order.type == MassiveCombatOrderType::Hold,
          "non-targeted order payload wrong");
  command = workspace.handle(
      press(InputEventType::LeftReleased, center(layout.order_buttons[0])),
      width, height);
  require(workspace.selection().contains(11),
          "release over an order button must not clear the selection");

  // Targeted order enters the pick state, then resolves on release.
  command = workspace.handle(
      press(InputEventType::LeftPressed, center(layout.order_buttons[2])),
      width, height);
  require(workspace.targeting(), "targeting pick state not entered");
  command = workspace.handle(
      press(InputEventType::LeftReleased, center(layout.order_buttons[2])),
      width, height);
  require(workspace.targeting() &&
              command.kind == BattleWorkspaceCommandKind::None,
          "release over the armed order button must keep the pick state");
  const auto hostile = workspace.project({60.f, 10.f}, width, height);
  command = workspace.handle(press(InputEventType::LeftReleased, hostile),
                             width, height);
  require(command.kind == BattleWorkspaceCommandKind::IssueOrder,
          "targeted order missing");
  require(command.order.target_formation_id &&
              *command.order.target_formation_id == 77,
          "targeted order must reference the picked formation");

  // Context order: right click on the hostile formation issues Engage.
  (void)workspace.handle(press(InputEventType::LeftPressed, own), width,
                         height);
  (void)workspace.handle(press(InputEventType::LeftReleased, own), width,
                         height);
  (void)workspace.handle(press(InputEventType::RightPressed, hostile), width,
                         height);
  command = workspace.handle(press(InputEventType::RightReleased, hostile),
                             width, height);
  require(command.kind == BattleWorkspaceCommandKind::IssueOrder &&
              command.order.type == MassiveCombatOrderType::Engage &&
              command.order.target_formation_id &&
              *command.order.target_formation_id == 77,
          "context engage order wrong");

  // Right click on open space issues an Advance objective.
  (void)workspace.handle(press(InputEventType::RightPressed, {400.f, 300.f}),
                         width, height);
  command = workspace.handle(press(InputEventType::RightReleased, {400.f, 300.f}),
                             width, height);
  require(command.order.type == MassiveCombatOrderType::Advance &&
              command.order.objective,
          "context advance order must carry an objective");
}

void speed_and_chrome() {
  constexpr int width = 1280, height = 720;
  const auto layout = BattleWorkspaceLayout::for_viewport(width, height);
  NativeBattleWorkspace workspace;
  workspace.open(snapshot(), 1, width, height);
  auto command = workspace.handle(
      press(InputEventType::LeftPressed, center(layout.play)), width, height);
  require(command.kind == BattleWorkspaceCommandKind::TogglePause,
          "play button must toggle pause");
  command = workspace.handle(
      press(InputEventType::LeftPressed, center(layout.speed)), width, height);
  require(command.kind == BattleWorkspaceCommandKind::CycleSpeed,
          "speed button must cycle");
  command = workspace.handle(
      press(InputEventType::LeftPressed, center(layout.menu)), width, height);
  require(command.kind == BattleWorkspaceCommandKind::Menu,
          "menu button missing");
  command = workspace.handle(press(InputEventType::EscapePressed, {}), width,
                             height);
  require(command.kind == BattleWorkspaceCommandKind::Menu,
          "escape must open the menu");
}

void snapshot_drops_departed_selection() {
  constexpr int width = 1280, height = 720;
  NativeBattleWorkspace workspace;
  workspace.open(snapshot(), 1, width, height);
  const auto own = workspace.project({-60.f, 0.f}, width, height);
  (void)workspace.handle(press(InputEventType::LeftPressed, own), width,
                         height);
  (void)workspace.handle(press(InputEventType::LeftReleased, own), width,
                         height);
  require(workspace.selected_count() == 1, "selection not established");
  auto next = snapshot();
  next.formations.erase(next.formations.begin());
  workspace.set_snapshot(std::move(next), .1);
  require(workspace.selection().empty(),
          "departed formation must leave the selection");
}

void observer_secrecy_rendering() {
  constexpr int width = 1280, height = 720;
  NativeBattleWorkspace workspace;
  auto view = snapshot();
  view.formations[2].ship_count_low = 80;
  view.formations[2].ship_count_high = 160;
  view.formations[2].is_exact = false;
  workspace.open(view, 1, width, height);
  DrawList draw;
  workspace.render(draw, width, height);
  const auto tokens = workspace.rendered_tokens();
  require(tokens > 0, "tokens missing");

  // The unidentified formation contributes only range-midpoint tokens; with a
  // 120-ship midpoint at fit zoom the per-formation cap stays below 28+lead.
  workspace.close();
  require(!workspace.visible() && workspace.snapshot() == nullptr,
          "close must release the snapshot");
  workspace.discard_campaign();
}

} // namespace

int main() {
  try {
    responsive_layout();
    open_fit_and_render();
    selection_rules();
    order_commands();
    speed_and_chrome();
    snapshot_drops_departed_selection();
    observer_secrecy_rendering();
  } catch (const std::exception &error) {
    std::cerr << "native battle workspace tests failed: " << error.what()
              << '\n';
    return 1;
  }
  std::cout << "native battle workspace tests passed\n";
  return 0;
}
