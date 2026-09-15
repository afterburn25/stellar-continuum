#include "native_shipyard_workspace.hpp"
#include "native_ui_layout.hpp"

#include <algorithm>
#include <iostream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <variant>

namespace {
using namespace stellar::native_map;
using namespace stellar::native_shipyard;
using namespace stellar::native_shipyard_ui;

void require(bool condition, std::string_view expression, int line) {
  if (!condition)
    throw std::runtime_error("Gate111 check failed at line " +
                             std::to_string(line) + ": " +
                             std::string(expression));
}

#define REQUIRE(expression) require((expression), #expression, __LINE__)

[[nodiscard]] Point center(UiRect bounds) {
  return {bounds.x + bounds.width * .5f,
          bounds.y + bounds.height * .5f};
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

[[nodiscard]] NativeShipDesign design(std::string id = "scout") {
  return {.id = std::move(id),
          .name = "Long-range Survey Vessel With A Clipped Name",
          .description = "A source-authored known design description.",
          .role = stellar::core::FleetRole::Scout,
          .industry_cost = 100.,
          .minimum_build_days_at_full_shipyard_rate = 5.,
          .credit_cost = 12.345,
          .population_cost_millions = .025,
          .strategic_speed = 2.,
          .maximum_leg_range_light_years = 8.,
          .fuel_endurance_light_years = 16.,
          .sensor_range = 3.f,
          .propulsion_generation = "Chemical",
          .formatted_credit_cost = "$12.35 SOL",
          .can_start = true,
          .will_queue = false,
          .minimum_source_population_millions = 500.,
          .population_source_colony_id = 2,
          .population_species_id = "human",
          .population_source_current_millions = 800.};
}

[[nodiscard]] NativeShipyardOrder order(std::string id = "order-1") {
  return {.order_id = std::move(id),
          .design_id = "scout",
          .design_name = "Long-range Survey Vessel",
          .active = true,
          .progress_fraction = .25,
          .industry_progress = 25.,
          .industry_remaining = 75.,
          .authorization_credits = 12.345,
          .reserved_population_millions = .025,
          .can_cancel = true,
          .refund_credits = 9.876,
          .formatted_refund = "$9.88 SOL"};
}

[[nodiscard]] NativeShipyardView view(std::uint64_t generation = 1) {
  return {.campaign_generation = generation,
          .shipyard_revision = 4,
          .player_civilization_id = 7,
          .home_system_id = 1,
          .orbital_shipyard_complete = true,
          .currency = {.name = "Solar",
                       .code = "SOL",
                       .symbol = "$",
                       .local_units_per_budget_unit = 1.},
          .treasury_credits = 90.,
          .formatted_treasury = "$90 SOL",
          .available_industry = 20.,
          .largest_owned_colony_population_millions = 800.,
          .pending_build_count = 0,
          .maximum_pending_builds = 4,
          .available_designs = {design()}};
}

void layout_is_contained_and_action_stays_visible() {
  for (const auto [width, height] :
       {std::pair{1280, 720}, std::pair{1920, 1080},
        std::pair{2560, 1440}, std::pair{3840, 2160},
        std::pair{1280, 1080}}) {
    const auto layout = ShipyardWorkspaceLayout::for_viewport(width, height);
    const UiRect viewport{0, 0, static_cast<float>(width),
                          static_cast<float>(height)};
    const auto navigation = NativeUiLayout::for_viewport(width, height);
    REQUIRE(contains(viewport, layout.surface));
    REQUIRE(layout.surface.x >=
            navigation.research.x + navigation.research.width);
    for (const auto bounds : {layout.designs, layout.design_details,
                              layout.orders, layout.readiness,
                              layout.feedback, layout.action})
      REQUIRE(contains(layout.surface, bounds));
    REQUIRE(!overlaps(layout.designs, layout.design_details));
    REQUIRE(!overlaps(layout.design_details, layout.orders));
    REQUIRE(!overlaps(layout.readiness, layout.feedback));
    REQUIRE(!overlaps(layout.feedback, layout.action));
    REQUIRE(layout.action.height >= 40.f * layout.scale);
  }
}

void start_and_cancel_use_real_mouse_hit_bounds() {
  NativeShipyardWorkspace workspace;
  workspace.open();
  workspace.set_view(view());
  const auto layout = ShipyardWorkspaceLayout::for_viewport(1280, 720);
  auto command = workspace.handle(
      {InputEventType::LeftPressed, center(layout.action)}, 1280, 720);
  REQUIRE(command.captured);
  REQUIRE(command.kind == ShipyardWorkspaceCommandKind::Start);
  REQUIRE(command.id == "scout");

  auto with_order = view();
  with_order.orders = {order()};
  workspace.set_view(std::move(with_order));
  const UiRect first_order{layout.orders.x,
                           layout.orders.y + 27.f * layout.scale,
                           layout.orders.width, 68.f * layout.scale};
  command = workspace.handle(
      {InputEventType::LeftPressed, center(first_order)}, 1280, 720);
  REQUIRE(command.captured && command.kind == ShipyardWorkspaceCommandKind::None);
  REQUIRE(workspace.selected_order_id() == std::optional<std::string>{"order-1"});
  command = workspace.handle(
      {InputEventType::LeftPressed, center(layout.action)}, 1280, 720);
  REQUIRE(command.captured &&
         command.kind == ShipyardWorkspaceCommandKind::PrepareCancel);
  auto refreshed = view();
  refreshed.shipyard_revision = 5;
  refreshed.orders = {order()};
  workspace.set_view(std::move(refreshed));
  REQUIRE(workspace.arm_cancel_confirmation(command.id));
  auto changed_quote = view();
  changed_quote.shipyard_revision = 6;
  changed_quote.orders = {order()};
  changed_quote.orders.front().formatted_refund = "$8.00 SOL";
  workspace.set_view(std::move(changed_quote));
  command = workspace.handle(
      {InputEventType::LeftPressed, center(layout.action)}, 1280, 720);
  REQUIRE(command.kind == ShipyardWorkspaceCommandKind::PrepareCancel);
  REQUIRE(workspace.arm_cancel_confirmation(command.id));
  command = workspace.handle(
      {InputEventType::LeftPressed, center(layout.action)}, 1280, 720);
  REQUIRE(command.kind == ShipyardWorkspaceCommandKind::Cancel);
  REQUIRE(command.id == "order-1");
}

void campaign_replacement_discards_old_order_context() {
  NativeShipyardWorkspace workspace;
  workspace.open();
  auto first = view(8);
  first.available_designs.clear();
  first.orders = {order("old-order")};
  workspace.set_view(std::move(first));
  REQUIRE(workspace.selected_order_id() ==
         std::optional<std::string>{"old-order"});
  workspace.set_notice("Old campaign cancellation accepted.", true);

  auto replacement = view(9);
  replacement.available_designs = {design("new-design")};
  workspace.set_view(std::move(replacement));
  REQUIRE(!workspace.selected_order_id());
  REQUIRE(workspace.selected_design_id() ==
         std::optional<std::string>{"new-design"});
  const auto layout = ShipyardWorkspaceLayout::for_viewport(1280, 720);
  const auto command = workspace.handle(
      {InputEventType::LeftPressed, center(layout.action)}, 1280, 720);
  REQUIRE(command.kind == ShipyardWorkspaceCommandKind::Start);
  REQUIRE(command.id == "new-design");
}

void empty_and_locked_states_render_without_invented_items() {
  NativeShipyardWorkspace workspace;
  workspace.open();
  NativeShipyardView empty;
  empty.campaign_generation = 1;
  empty.shipyard_revision = 1;
  workspace.set_view(std::move(empty));
  DrawList draw;
  workspace.render(draw, 1280, 720);
  bool empty_designs{};
  bool empty_orders{};
  for (const auto &item : draw.overlay)
    if (const auto *label = std::get_if<Text>(&item)) {
      empty_designs |= label->value.find("No known designs") != std::string::npos;
      empty_orders |= label->value.find("No ships") != std::string::npos;
      if (label->clip)
        REQUIRE(contains({0, 0, 1280, 720}, *label->clip));
    }
  REQUIRE(empty_designs && empty_orders);
}

void full_720p_content_keeps_cost_feedback_and_action_clipped() {
  NativeShipyardWorkspace workspace;
  workspace.open();
  auto content = view();
  content.available_designs.front().description = std::string(500, 'D');
  workspace.set_view(std::move(content));
  workspace.set_notice(std::string(500, 'E'), false);
  DrawList draw;
  workspace.render(draw, 1280, 720);
  const auto layout = ShipyardWorkspaceLayout::for_viewport(1280, 720);
  bool cost{};
  bool readiness{};
  bool action{};
  bool capped_error{};
  for (const auto &item : draw.overlay)
    if (const auto *label = std::get_if<Text>(&item)) {
      if (label->value.find("Authorization $12.35 SOL") != std::string::npos) {
        cost = true;
        REQUIRE(label->clip && same_rect(*label->clip, layout.readiness));
      }
      readiness |= label->value.find("Ready to build") !=
                   std::string::npos;
      action |= label->value == "START BUILD";
      if (!label->value.empty() && label->value.front() == 'E') {
        capped_error = true;
        REQUIRE(label->value.size() <= 180);
        REQUIRE(label->clip && same_rect(*label->clip, layout.feedback));
      }
      if (label->clip)
        REQUIRE(contains(layout.surface, *label->clip));
    }
  REQUIRE(cost && readiness && action && capped_error);
}

} // namespace

int run_tests() {
  layout_is_contained_and_action_stays_visible();
  start_and_cancel_use_real_mouse_hit_bounds();
  campaign_replacement_discards_old_order_context();
  empty_and_locked_states_render_without_invented_items();
  full_720p_content_keeps_cost_feedback_and_action_clipped();
  return 0;
}

int main() try {
  return run_tests();
} catch (const std::exception &error) {
  std::cerr << error.what() << '\n';
  return 1;
}
