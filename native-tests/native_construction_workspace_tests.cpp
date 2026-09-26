#include "native_construction_workspace.hpp"
#include "native_ui_layout.hpp"

#include <iostream>
#include <cmath>
#include <limits>
#include <stdexcept>
#include <string>
#include <string_view>
#include <variant>

namespace {
using namespace stellar::native_construction;
using namespace stellar::native_construction_ui;
using namespace stellar::native_map;

void require(bool condition, std::string_view expression, int line) {
  if (!condition)
    throw std::runtime_error("Gate114 check failed at line " +
                             std::to_string(line) + ": " +
                             std::string(expression));
}
#define REQUIRE(expression) require((expression), #expression, __LINE__)

[[nodiscard]] Point center(UiRect value) {
  return {value.x + value.width * .5f, value.y + value.height * .5f};
}
[[nodiscard]] bool contains(UiRect outer, UiRect inner) {
  return inner.x >= outer.x && inner.y >= outer.y &&
         inner.x + inner.width <= outer.x + outer.width &&
         inner.y + inner.height <= outer.y + outer.height;
}
[[nodiscard]] bool overlaps(UiRect left, UiRect right) {
  return left.x < right.x + right.width &&
         right.x < left.x + left.width &&
         left.y < right.y + right.height &&
         right.y < left.y + left.height;
}
[[nodiscard]] bool same_rect(UiRect left, UiRect right) {
  return left.x == right.x && left.y == right.y &&
         left.width == right.width && left.height == right.height;
}

[[nodiscard]] NativeConstructionProject project(std::string id = "yard") {
  NativeConstructionProject value;
  value.id = std::move(id);
  value.name = "Orbital Shipyard With A Long Source Name";
  value.description = "A source-authored construction description.";
  value.category = stellar::core::ConstructionCategory::Orbital;
  value.industry_cost = 900.;
  value.credit_cost = 70.;
  value.upkeep_credits_per_day = .25;
  value.industry_per_day = 30.;
  value.formatted_credit_cost = "$700M UED";
  value.formatted_upkeep_rate = "-$2.5M UED/day";
  value.requirements = {"Orbital Industry"};
  value.industry_remaining = 900.;
  value.minimum_days_remaining = 30.;
  value.formatted_authorization = "$700M UED";
  value.formatted_cancellation_refund = "$700M UED";
  value.start = {true, true, false, "Ready to begin."};
  value.queue = {true, false, true, "Ready to queue."};
  return value;
}

[[nodiscard]] NativeConstructionView view(std::uint64_t generation = 1,
                                          std::uint64_t revision = 3) {
  NativeConstructionView value;
  value.campaign_generation = generation;
  value.construction_revision = revision;
  value.player_civilization_id = 7;
  value.home_system_id = 1;
  value.currency = {"United Earth Dollar", "UED", "$", 10'000'000.};
  value.treasury_credits = 100.;
  value.available_industry = 200.;
  value.formatted_treasury = "$1B UED";
  value.projects = {project()};
  return value;
}

void responsive_layout_contains_full_actions() {
  for (const auto [width, height] :
       {std::pair{1280, 720}, std::pair{1920, 1080},
        std::pair{2560, 1440}, std::pair{3840, 2160},
        std::pair{1280, 1080}}) {
    const auto layout = ConstructionWorkspaceLayout::for_viewport(width, height);
    const UiRect viewport{0, 0, static_cast<float>(width),
                          static_cast<float>(height)};
    const auto navigation = NativeUiLayout::for_viewport(width, height);
    REQUIRE(contains(viewport, layout.surface));
    REQUIRE(layout.surface.x >=
            navigation.inspect.x + navigation.inspect.width);
    for (const auto bounds : {layout.projects, layout.details, layout.orders,
                              layout.costs, layout.feedback,
                              layout.primary_action,
                              layout.secondary_action})
      REQUIRE(contains(layout.surface, bounds));
    REQUIRE(!overlaps(layout.projects, layout.details));
    REQUIRE(!overlaps(layout.details, layout.orders));
    REQUIRE(!overlaps(layout.costs, layout.feedback));
    REQUIRE(!overlaps(layout.feedback, layout.primary_action));
    REQUIRE(!overlaps(layout.primary_action, layout.secondary_action));
    // The command HUD's bottom context plate must stay clear of the action
    // row and its feedback hint.
    const auto hud = CommandHudLayout::make(width, height);
    REQUIRE(!overlaps(hud.context, layout.primary_action));
    REQUIRE(!overlaps(hud.context, layout.secondary_action));
    REQUIRE(!overlaps(hud.context, layout.feedback));
  }
}

void mouse_routes_start_queue_and_stable_cancel_confirmation() {
  NativeConstructionWorkspace workspace;
  workspace.open();
  workspace.set_view(view());
  const auto layout = ConstructionWorkspaceLayout::for_viewport(1280, 720);
  auto command = workspace.handle(
      {InputEventType::LeftPressed, center(layout.primary_action)}, 1280, 720);
  REQUIRE(command.kind == ConstructionWorkspaceCommandKind::Start);
  REQUIRE(command.project_id == "yard");
  command = workspace.handle(
      {InputEventType::LeftPressed, center(layout.secondary_action)}, 1280, 720);
  REQUIRE(command.kind == ConstructionWorkspaceCommandKind::Queue);

  auto active = view(1, 4);
  active.projects.front().active = true;
  active.projects.front().start.enabled = false;
  active.projects.front().queue.enabled = false;
  workspace.set_view(std::move(active));
  command = workspace.handle(
      {InputEventType::LeftPressed, center(layout.secondary_action)}, 1280, 720);
  REQUIRE(command.kind == ConstructionWorkspaceCommandKind::PrepareCancel);
  auto refreshed = view(1, 5);
  refreshed.projects.front().active = true;
  refreshed.projects.front().formatted_cancellation_refund = "$650M UED";
  workspace.set_view(std::move(refreshed));
  REQUIRE(workspace.arm_cancel_confirmation(command.project_id));
  REQUIRE(workspace.confirmation_open());

  auto changed = view(1, 6);
  changed.projects.front().active = true;
  changed.projects.front().formatted_cancellation_refund = "$600M UED";
  workspace.set_view(std::move(changed));
  REQUIRE(!workspace.confirmation_open());
  command = workspace.handle(
      {InputEventType::LeftPressed, center(layout.secondary_action)}, 1280, 720);
  REQUIRE(command.kind == ConstructionWorkspaceCommandKind::PrepareCancel);
  REQUIRE(workspace.arm_cancel_confirmation(command.project_id));
  command = workspace.handle(
      {InputEventType::LeftPressed, center(layout.secondary_action)}, 1280, 720);
  REQUIRE(command.kind == ConstructionWorkspaceCommandKind::Cancel);
}

void replacement_and_empty_view_do_not_retain_old_context() {
  NativeConstructionWorkspace workspace;
  workspace.open();
  workspace.set_view(view(8));
  workspace.set_notice("Old campaign result", true);
  NativeConstructionView replacement;
  replacement.campaign_generation = 9;
  replacement.construction_revision = 1;
  workspace.set_view(std::move(replacement));
  REQUIRE(!workspace.selected_project_id());
  DrawList draw;
  workspace.render(draw, 1280, 720);
  bool honest_empty{};
  for (const auto &item : draw.overlay)
    if (const auto *label = std::get_if<Text>(&item))
      honest_empty |= label->value.find("No known projects") !=
                      std::string::npos;
  REQUIRE(honest_empty);
}

void full_720p_content_keeps_cost_feedback_and_actions_clipped() {
  NativeConstructionWorkspace workspace;
  workspace.open();
  auto content = view();
  content.projects.front().description = std::string(600, 'D');
  content.projects.front().start.message = std::string(300, 'S');
  content.projects.front().queue.message = std::string(300, 'Q');
  workspace.set_view(std::move(content));
  DrawList draw;
  workspace.render(draw, 1280, 720);
  const auto layout = ConstructionWorkspaceLayout::for_viewport(1280, 720);
  bool authorization{};
  bool upkeep{};
  bool start{};
  bool queue{};
  bool capped_feedback{};
  for (const auto &item : draw.overlay)
    if (const auto *label = std::get_if<Text>(&item)) {
      authorization |= label->value.find("Authorization $700M UED") !=
                       std::string::npos;
      upkeep |= label->value.find("Upkeep -$2.5M UED/day") !=
                std::string::npos;
      start |= label->value == "START NOW";
      queue |= label->value == "QUEUE";
      if (label->value.starts_with("Start: S")) {
        capped_feedback = true;
        REQUIRE(label->value.size() <= 180);
        REQUIRE(label->clip && same_rect(*label->clip, layout.feedback));
      }
      if (label->clip) REQUIRE(contains(layout.surface, *label->clip));
    }
  REQUIRE(authorization && upkeep && start && queue && capped_feedback);
}

void disabled_actions_explain_the_blocker_on_hover() {
  NativeConstructionWorkspace workspace;
  workspace.open();
  auto content = view();
  content.projects.front().start.enabled = false;
  content.projects.front().start.message =
      "Insufficient authorization for this project.";
  content.projects.front().queue.enabled = false;
  content.projects.front().queue.message = "The production line is full.";
  workspace.set_view(std::move(content));
  const auto layout = ConstructionWorkspaceLayout::for_viewport(1280, 720);
  const auto explains = [](const DrawList &draw, std::string_view reason) {
    for (const auto &item : draw.overlay)
      if (const auto *label = std::get_if<Text>(&item))
        if (label->value == reason) return true;
    return false;
  };
  DrawList draw;
  (void)workspace.handle({InputEventType::PointerMove, {4.f, 4.f}}, 1280, 720);
  workspace.render(draw, 1280, 720);
  REQUIRE(
      !explains(draw, "Insufficient authorization for this project."));
  DrawList hovered;
  (void)workspace.handle(
      {InputEventType::PointerMove, center(layout.primary_action)}, 1280, 720);
  workspace.render(hovered, 1280, 720);
  REQUIRE(
      explains(hovered, "Insufficient authorization for this project."));
  DrawList queued;
  (void)workspace.handle(
      {InputEventType::PointerMove, center(layout.secondary_action)}, 1280,
      720);
  workspace.render(queued, 1280, 720);
  REQUIRE(explains(queued, "The production line is full."));
}

void progress_bar_clamps_nonfinite_fraction_inside_orders() {
  NativeConstructionWorkspace workspace;
  workspace.open();
  auto content = view();
  content.projects.front().active = true;
  content.projects.front().progress_fraction =
      std::numeric_limits<double>::quiet_NaN();
  workspace.set_view(std::move(content));
  DrawList draw;
  workspace.render(draw, 1280, 720);
  const auto layout = ConstructionWorkspaceLayout::for_viewport(1280, 720);
  bool thin_track{};
  for (const auto &item : draw.overlay)
    if (const auto *rectangle = std::get_if<FilledRectangle>(&item)) {
      REQUIRE(std::isfinite(rectangle->bounds.x));
      REQUIRE(std::isfinite(rectangle->bounds.y));
      REQUIRE(std::isfinite(rectangle->bounds.width));
      REQUIRE(std::isfinite(rectangle->bounds.height));
      REQUIRE(contains(layout.surface, rectangle->bounds));
      // Themed section headers also draw thin rule lines; the progress
      // track is the thin rect inside the orders panel.
      if (rectangle->bounds.height <= 3.f * layout.scale &&
          contains(layout.orders, rectangle->bounds))
        thin_track = true;
    }
  REQUIRE(thin_track);
}

void long_status_list_uses_72_pitch_and_canonical_presentation_order() {
  NativeConstructionWorkspace workspace;
  workspace.open();
  auto content = view();
  content.projects.clear();
  for (int index = 0; index < 3; ++index) {
    auto value = project("completed-" + std::to_string(index));
    value.complete = true;
    value.start.enabled = false;
    value.queue.enabled = false;
    value.progress_fraction = 1.;
    content.projects.push_back(std::move(value));
  }
  for (int position = 10; position >= 1; --position) {
    auto value = project("queued-" + std::to_string(position));
    value.queued = true;
    value.queue_position = position;
    value.start.enabled = false;
    value.queue.enabled = false;
    content.projects.push_back(std::move(value));
  }
  auto active = project("active-last-in-catalog");
  active.active = true;
  active.start.enabled = false;
  active.queue.enabled = false;
  content.projects.push_back(std::move(active));
  workspace.set_view(std::move(content));
  const auto layout = ConstructionWorkspaceLayout::for_viewport(1280, 720);
  const UiRect rows{layout.orders.x,
                    layout.orders.y + 27.f * layout.scale,
                    layout.orders.width,
                    layout.orders.height - 27.f * layout.scale};
  auto command = workspace.handle(
      {InputEventType::LeftPressed,
       {rows.x + 20.f * layout.scale, rows.y + 34.f * layout.scale}},
      1280, 720);
  REQUIRE(command.captured);
  REQUIRE(workspace.selected_project_id() ==
          std::optional<std::string>{"active-last-in-catalog"});
  command = workspace.handle(
      {InputEventType::LeftPressed,
       {rows.x + 20.f * layout.scale,
        rows.y + (72.f + 34.f) * layout.scale}},
      1280, 720);
  REQUIRE(command.captured);
  REQUIRE(workspace.selected_project_id() ==
          std::optional<std::string>{"queued-1"});
  command = workspace.handle(
      {.type = InputEventType::Wheel,
       .position = center(layout.orders),
       .wheel_y = -100.f},
      1280, 720);
  REQUIRE(command.captured);
  command = workspace.handle(
      {InputEventType::LeftPressed,
       {rows.x + 20.f * layout.scale,
        rows.y + rows.height - 34.f * layout.scale}},
      1280, 720);
  REQUIRE(command.captured);
  REQUIRE(workspace.selected_project_id() ==
          std::optional<std::string>{"completed-2"});
}
void keyboard_focus_traversal() {
  constexpr std::uint32_t kTab = 9u;
  constexpr std::uint32_t kReturn = 13u;
  constexpr std::uint32_t kSpace = 32u;
  constexpr std::uint32_t kHome = 0x4000004au;
  constexpr std::uint32_t kEnd = 0x4000004du;
  constexpr std::uint32_t kDigit5 = '5';
  const int width = 1280, height = 720;
  NativeConstructionWorkspace workspace;
  workspace.open();
  workspace.set_view(view());
  const auto key = [&](std::uint32_t k, bool shift = false) {
    InputEvent event{InputEventType::KeyPressed};
    event.key = k;
    event.shift = shift;
    return workspace.handle(event, width, height);
  };
  REQUIRE(workspace.focus() < 0);
  const auto ring_layout =
      ConstructionWorkspaceLayout::for_viewport(width, height);
  REQUIRE(workspace.focused_label(ring_layout).empty());
  // Ordered ring: close, project row, primary action, secondary action.
  REQUIRE(key(kTab).captured && workspace.focus() == 0);
  REQUIRE(workspace.focused_label(ring_layout) == "Close construction");
  REQUIRE(key(kTab).captured && workspace.focus() == 1);
  REQUIRE(workspace.focused_label(ring_layout) ==
          "Orbital Shipyard With A Long Source Name");
  REQUIRE(key(kTab, true).captured && workspace.focus() == 0);
  REQUIRE(key(kEnd).captured && workspace.focus() == 3);
  REQUIRE(workspace.focused_label(ring_layout) == "Queue");
  REQUIRE(key(kHome).captured && workspace.focus() == 0);
  // Row activation keeps focus and selects without a command.
  (void)key(kTab);
  REQUIRE(key(kSpace).captured &&
          workspace.focus() == 1 && workspace.visible());
  // Primary action issues Start through the pointer dispatch.
  (void)key(kTab);
  auto command = key(kReturn);
  REQUIRE(command.kind == ConstructionWorkspaceCommandKind::Start);
  REQUIRE(command.project_id == "yard" && workspace.focus() == 2);
  // Active project narrows the ring; secondary drives the two-stage cancel.
  auto active = view(1, 4);
  active.projects.front().active = true;
  workspace.set_view(std::move(active));
  REQUIRE(workspace.focus() == 2);
  command = key(kEnd);
  const int secondary = workspace.focus();
  command = key(kReturn);
  REQUIRE(command.kind == ConstructionWorkspaceCommandKind::PrepareCancel &&
          workspace.focus() == secondary);
  REQUIRE(workspace.arm_cancel_confirmation(command.project_id));
  command = key(kReturn);
  REQUIRE(command.kind == ConstructionWorkspaceCommandKind::Cancel &&
          workspace.focus() == secondary);
  // Close activation closes the surface; unrelated keys pass through.
  REQUIRE(!key(kDigit5).captured);
  (void)key(kHome);
  REQUIRE(key(kReturn).captured && !workspace.visible());
  // Pointer presses inside the surface clear focus.
  workspace.open();
  workspace.set_view(view());
  (void)key(kTab);
  const auto layout = ConstructionWorkspaceLayout::for_viewport(width, height);
  (void)workspace.handle(
      {InputEventType::LeftPressed, center(layout.projects)}, width, height);
  REQUIRE(workspace.focus() < 0);
}
} // namespace

int main() try {
  responsive_layout_contains_full_actions();
  mouse_routes_start_queue_and_stable_cancel_confirmation();
  replacement_and_empty_view_do_not_retain_old_context();
  full_720p_content_keeps_cost_feedback_and_actions_clipped();
  disabled_actions_explain_the_blocker_on_hover();
  progress_bar_clamps_nonfinite_fraction_inside_orders();
  long_status_list_uses_72_pitch_and_canonical_presentation_order();
  keyboard_focus_traversal();
  return 0;
} catch (const std::exception &error) {
  std::cerr << error.what() << '\n';
  return 1;
}
