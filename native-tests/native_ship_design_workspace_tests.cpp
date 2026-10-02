#include "native_ship_design_workspace.hpp"

#include <stellar/core/ship_components.hpp>

#include <iostream>
#include <stdexcept>
#include <string>
#include <string_view>

namespace {
using namespace stellar::native_map;
using namespace stellar::native_ship_design;
using namespace stellar::native_ship_design_ui;

void require(bool condition, std::string_view expression, int line) {
  if (!condition)
    throw std::runtime_error("Ship design workspace check failed at line " +
                             std::to_string(line) + ": " +
                             std::string(expression));
}

#define REQUIRE(expression) require((expression), #expression, __LINE__)

[[nodiscard]] Point center(UiRect bounds) {
  return {bounds.x + bounds.width * .5f,
          bounds.y + bounds.height * .5f};
}

[[nodiscard]] InputEvent press(Point position) {
  InputEvent event{InputEventType::LeftPressed};
  event.position = position;
  return event;
}

[[nodiscard]] InputEvent release(Point position) {
  InputEvent event{InputEventType::LeftReleased};
  event.position = position;
  return event;
}

[[nodiscard]] NativeHullOption hull(std::string id,
                                    std::vector<stellar::core::ShipComponentSlot>
                                        slots) {
  NativeHullOption option;
  option.id = std::move(id);
  option.name = "Test Hull " + option.id;
  option.description = "Hull description.";
  option.role = stellar::core::FleetRole::Scout;
  option.industry_cost = 40.;
  option.credit_cost = 10.;
  option.strategic_speed = 2.;
  option.slot_count = static_cast<int>(slots.size());
  option.slots = std::move(slots);
  option.required_slots = {stellar::core::ShipComponentSlot::Engine};
  return option;
}

[[nodiscard]] NativeComponentOption component(
    std::string id, stellar::core::ShipComponentSlot slot) {
  NativeComponentOption option;
  option.id = std::move(id);
  option.name = "Component " + option.id;
  option.slot = slot;
  option.slot_name = std::string(
      stellar::core::ship_component_slot_name(slot));
  option.industry_cost = 5.;
  option.credit_cost = 2.;
  return option;
}

[[nodiscard]] NativeAuthoredDesignRow design(std::string id) {
  NativeAuthoredDesignRow row;
  row.id = std::move(id);
  row.name = "Authored " + row.id;
  row.hull_id = "scout_frame";
  row.hull_name = "Test Hull scout_frame";
  row.valid = true;
  row.can_retire = true;
  row.max_hull = 100.;
  row.max_shields = 25.;
  row.weapon_damage = 4.;
  return row;
}

[[nodiscard]] NativeShipDesignView view(std::uint64_t generation = 1) {
  NativeShipDesignView out;
  out.campaign_generation = generation;
  out.design_revision = 1;
  out.player_civilization_id = 7;
  out.hulls = {hull("scout_frame",
                    {stellar::core::ShipComponentSlot::Engine,
                     stellar::core::ShipComponentSlot::Sensor,
                     stellar::core::ShipComponentSlot::Weapon}),
               hull("freighter_frame",
                    {stellar::core::ShipComponentSlot::Engine,
                     stellar::core::ShipComponentSlot::Cargo})};
  out.components = {
      component("ion_drive", stellar::core::ShipComponentSlot::Engine),
      component("warp_drive", stellar::core::ShipComponentSlot::Engine),
      component("array", stellar::core::ShipComponentSlot::Sensor),
      component("battery", stellar::core::ShipComponentSlot::Weapon),
      component("hold", stellar::core::ShipComponentSlot::Cargo)};
  out.designs = {design("alpha"), design("beta")};
  return out;
}

void layout_holds_at_common_resolutions() {
  for (const auto [width, height] :
       {std::pair{1280, 720}, std::pair{1920, 1080}, std::pair{2560, 1440}}) {
    const auto layout = ShipDesignWorkspaceLayout::for_viewport(width, height);
    REQUIRE(layout.panel.x >= 0.f && layout.panel.y >= 0.f);
    REQUIRE(layout.panel.x + layout.panel.width <= width + .5f);
    REQUIRE(layout.panel.y + layout.panel.height <= height + .5f);
    REQUIRE(layout.list.width > 0.f && layout.editor.width > 0.f &&
            layout.details.width > 0.f);
    REQUIRE(layout.commit.width > 0.f && layout.retire.width > 0.f);
  }
}

void list_selection_and_closing() {
  NativeShipDesignWorkspace workspace;
  workspace.open();
  // While the first view is loading the surface still captures input.
  REQUIRE(workspace.handle(press({10, 10}), 1920, 1080).captured);
  workspace.set_view(view());
  const auto layout = ShipDesignWorkspaceLayout::for_viewport(1920, 1080);

  // Click the second design row (press owns the pointer; release selects).
  const float row_height = 46.f * layout.scale;
  const float y = layout.list.y + 34.f * layout.scale + row_height + 4.f;
  (void)workspace.handle(press({layout.list.x + 20.f, y + 10.f}),
                         1920, 1080);
  const auto select = workspace.handle(
      release({layout.list.x + 20.f, y + 10.f}), 1920, 1080);
  REQUIRE(select.kind == ShipDesignWorkspaceCommandKind::SelectDesign);
  REQUIRE(select.captured && select.design_id == "beta");
  REQUIRE(workspace.selected_design_id() &&
          *workspace.selected_design_id() == "beta");

  // Click outside the panel closes the bureau.
  REQUIRE(workspace.handle(press({4.f, 4.f}), 1920, 1080).kind ==
          ShipDesignWorkspaceCommandKind::Close);
  REQUIRE(!workspace.visible());
}

void hull_and_component_cycling() {
  NativeShipDesignWorkspace workspace;
  workspace.open();
  workspace.set_view(view());
  const auto layout = ShipDesignWorkspaceLayout::for_viewport(1920, 1080);

  // Hull row's right arrow produces a CycleHull command.
  const float arrow = 30.f * layout.scale;
  const UiRect next{layout.hull_row.x + layout.hull_row.width - arrow -
                        6.f * layout.scale,
                    layout.hull_row.y + 2.f * layout.scale, arrow,
                    layout.hull_row.height - 4.f * layout.scale};
  (void)workspace.handle(press(center(next)), 1920, 1080);
  auto command = workspace.handle(release(center(next)), 1920, 1080);
  REQUIRE(command.kind == ShipDesignWorkspaceCommandKind::CycleHull &&
          command.direction == 1);
  workspace.cycle_hull_command(command.direction);
  REQUIRE(workspace.draft_hull_id() == "freighter_frame");
  workspace.cycle_hull_command(-1);
  REQUIRE(workspace.draft_hull_id() == "scout_frame");

  // Required engine slot is pre-filled; cycling moves through the catalog.
  const auto first = workspace.draft_component_ids();
  REQUIRE(first.size() == 1 && first.front() == "ion_drive");
  workspace.cycle_component_command(0, 1);
  REQUIRE(workspace.draft_component_ids().front() == "warp_drive");
  workspace.cycle_component_command(0, 1);
  REQUIRE(workspace.draft_component_ids().front() == "ion_drive");

  // Optional sensor slot can cycle through empty.
  workspace.cycle_component_command(1, 1);
  auto ids = workspace.draft_component_ids();
  REQUIRE(ids.size() == 2 &&
          ids.back() == "array");
  workspace.cycle_component_command(1, 1);
  ids = workspace.draft_component_ids();
  REQUIRE(ids.size() == 1);

  // Draft is structurally valid and committable.
  REQUIRE(workspace.draft_committable());
  REQUIRE(workspace.draft_issues().empty());
}

void commit_and_retire_release_gating() {
  NativeShipDesignWorkspace workspace;
  workspace.open();
  workspace.set_view(view());
  const auto layout = ShipDesignWorkspaceLayout::for_viewport(1920, 1080);

  // Commit is a press/release pair on the commit button.
  REQUIRE(workspace.handle(press(center(layout.commit)), 1920, 1080)
              .captured);
  auto command = workspace.handle(release(center(layout.commit)), 1920, 1080);
  REQUIRE(command.kind == ShipDesignWorkspaceCommandKind::Commit);

  // Press inside, release outside: no command.
  (void)workspace.handle(press(center(layout.commit)), 1920, 1080);
  command = workspace.handle(release({4.f, 4.f}), 1920, 1080);
  REQUIRE(command.kind != ShipDesignWorkspaceCommandKind::Commit);

  // Retire requires a selected design plus a confirmation press.
  const Point first_row{layout.list.x + 20.f,
                        layout.list.y + 34.f * layout.scale + 10.f};
  (void)workspace.handle(press(first_row), 1920, 1080);
  (void)workspace.handle(release(first_row), 1920, 1080);
  (void)workspace.handle(press(center(layout.retire)), 1920, 1080);
  command = workspace.handle(release(center(layout.retire)), 1920, 1080);
  REQUIRE(command.kind != ShipDesignWorkspaceCommandKind::Retire);
  REQUIRE(workspace.retire_confirmation_open());
  (void)workspace.handle(press(center(layout.retire)), 1920, 1080);
  command = workspace.handle(release(center(layout.retire)), 1920, 1080);
  REQUIRE(command.kind == ShipDesignWorkspaceCommandKind::Retire &&
          command.design_id == "alpha");
}

void keyboard_focus_cycles_actions() {
  NativeShipDesignWorkspace workspace;
  workspace.open();
  workspace.set_view(view());
  constexpr std::uint32_t kTab = 9u, kReturn = 13u;
  InputEvent tab{InputEventType::KeyPressed};
  tab.key = kTab;
  (void)workspace.handle(tab, 1920, 1080);
  REQUIRE(workspace.focus() == 0);
  (void)workspace.handle(tab, 1920, 1080);
  REQUIRE(workspace.focus() == 1);
  (void)workspace.handle(tab, 1920, 1080);
  REQUIRE(workspace.focus() == 2);
  (void)workspace.handle(tab, 1920, 1080);
  REQUIRE(workspace.focus() == 0);
  const auto layout = ShipDesignWorkspaceLayout::for_viewport(1920, 1080);
  REQUIRE(!workspace.focused_label(layout).empty());

  // Enter on the commit focus emits a commit command.
  InputEvent enter{InputEventType::KeyPressed};
  enter.key = kReturn;
  const auto command = workspace.handle(enter, 1920, 1080);
  REQUIRE(command.kind == ShipDesignWorkspaceCommandKind::Commit);

  // Escape closes.
  InputEvent escape{InputEventType::EscapePressed};
  REQUIRE(workspace.handle(escape, 1920, 1080).kind ==
          ShipDesignWorkspaceCommandKind::Close);
  REQUIRE(!workspace.visible());
}

void generation_change_resets_draft() {
  NativeShipDesignWorkspace workspace;
  workspace.open();
  workspace.set_view(view(1));
  workspace.cycle_hull_command(1);
  REQUIRE(workspace.draft_hull_id() == "freighter_frame");
  workspace.set_view(view(2));
  REQUIRE(workspace.draft_hull_id() == "scout_frame");
  workspace.discard_campaign();
  REQUIRE(!workspace.view());
}

} // namespace

int main() try {
  layout_holds_at_common_resolutions();
  list_selection_and_closing();
  hull_and_component_cycling();
  commit_and_retire_release_gating();
  keyboard_focus_cycles_actions();
  generation_change_resets_draft();
  std::cout << "Native ship design workspace tests passed\n";
  return 0;
} catch (const std::exception &error) {
  std::cerr << "Native ship design workspace test failed: " << error.what()
            << '\n';
  return 1;
}
