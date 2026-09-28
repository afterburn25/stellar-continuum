#include "native_system_workspace.hpp"
#include "native_system_view.hpp"

#include <algorithm>
#include <iostream>
#include <ranges>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <variant>

namespace {
using namespace stellar::native_map;
using namespace stellar::native_system;
using namespace stellar::native_system_travel;
using namespace stellar::native_system_ui;

void require(bool condition, std::string_view expression, int line) {
  if (!condition)
    throw std::runtime_error("Gate129 entry check failed at line " +
                             std::to_string(line) + ": " +
                             std::string(expression));
}
#define REQUIRE(expression) require((expression), #expression, __LINE__)

Point center(UiRect value) {
  return {value.x + value.width * .5f, value.y + value.height * .5f};
}

bool has_text(const DrawList &draw, std::string_view value) {
  return std::ranges::any_of(draw.overlay, [&](const auto &item) {
    const auto *label = std::get_if<Text>(&item);
    return label && label->value == value;
  });
}

NativeSystemSnapshot snapshot(std::uint64_t generation = 4) {
  NativeSystemSnapshot value;
  value.campaign_generation = generation;
  value.observer_civilization_id = 7;
  value.system_id = 2;
  value.catalog_name = "Observer Safe System";
  value.survey_level = stellar::core::SystemSurveyLevel::fully_surveyed;
  NativeSystemBody body;
  body.id = 9;
  body.orbit_index = 2;
  body.name = "Known Body";
  body.kind = stellar::core::PlanetaryBodyKind::Planet;
  body.radius_earth = 1.0;
  body.visual_class = NativeSystemBodyVisualClass::rocky;
  value.bodies.push_back(std::move(body));
  return value;
}
} // namespace

int main() {
  try {
    NativeSystemWorkspace workspace;
    workspace.open(snapshot(), 1280, 720);
    const auto spatial = project_system(*workspace.snapshot());
    const auto point = workspace.viewport()->world_to_screen(
        spatial.bodies.front().offset_x, spatial.bodies.front().offset_y);
    NativeSystemTravelSnapshot travel;
    travel.campaign_generation = 4;
    travel.observer_civilization_id = 7;
    travel.system_id = 2;
    travel.fleets.push_back({.fleet_id = 42,
                             .name = "Survey Flight",
                             .role = stellar::core::FleetRole::Scout,
                             .chart_position = {.x = 0.0, .y = 0.0},
                             .chart_target = {.x = 0.0, .y = 0.0}});
    workspace.refresh_travel(travel, 42);
    DrawList draw;
    workspace.render(draw, 1280, 720);
    REQUIRE(has_text(draw, "OWNED LOCAL FLEET"));

    auto command = workspace.handle(
        {InputEventType::LeftPressed, {point.x, point.y}},
                                    1280, 720);
    REQUIRE(command.captured && workspace.selected_body_id() == 9);
    draw = {};
    workspace.render(draw, 1280, 720);
    REQUIRE(has_text(draw, "Known Body"));
    REQUIRE(!has_text(draw, "OWNED LOCAL FLEET"));
    workspace.refresh_travel(travel, 42);
    draw = {};
    workspace.render(draw, 1280, 720);
    REQUIRE(has_text(draw, "Known Body"));
    REQUIRE(!has_text(draw, "OWNED LOCAL FLEET"));

    const auto fleet_point = local_fleet_anchor(
        travel.fleets.front(), spatial, *workspace.viewport());
    command = workspace.handle(
        {InputEventType::LeftPressed, fleet_point}, 1280, 720);
    REQUIRE(command.kind == SystemWorkspaceCommandKind::select_fleet &&
            command.target_id == 42);
    workspace.refresh_travel(travel, 42);
    draw = {};
    workspace.render(draw, 1280, 720);
    REQUIRE(has_text(draw, "OWNED LOCAL FLEET"));

    command = workspace.handle(
        {InputEventType::LeftPressed, {point.x, point.y}}, 1280, 720);
    REQUIRE(command.captured && workspace.selected_body_id() == 9);
    const auto layout = SystemWorkspaceLayout::for_viewport(1280, 720);
    command = workspace.handle(
        {InputEventType::LeftPressed, center(layout.colony_action)}, 1280, 720);
    REQUIRE(command.kind == SystemWorkspaceCommandKind::none &&
            command.captured);
    draw = {};
    workspace.render(draw, 1280, 720);
    REQUIRE(std::ranges::none_of(draw.overlay, [](const auto &item) {
      const auto *label = std::get_if<Text>(&item);
      return label && label->value == "MANAGE PLANET";
    }));

    workspace.set_colony_body(9);
    draw = {};
    workspace.render(draw, 1280, 720);
    REQUIRE(std::ranges::any_of(draw.overlay, [](const auto &item) {
      const auto *label = std::get_if<Text>(&item);
      return label && label->value == "MANAGE PLANET";
    }));
    command = workspace.handle(
        {InputEventType::LeftPressed, center(layout.colony_action)}, 1280, 720);
    REQUIRE(command.kind == SystemWorkspaceCommandKind::open_colony &&
            command.target_id == 9 && command.captured);

    workspace.set_colony_body(std::nullopt);
    command = workspace.handle(
        {InputEventType::LeftPressed, center(layout.colony_action)}, 1280, 720);
    REQUIRE(command.kind == SystemWorkspaceCommandKind::none &&
            command.captured);
    workspace.discard_campaign();
    REQUIRE(!workspace.visible());
    std::cout << "native system colony entry tests passed\n";
    return 0;
  } catch (const std::exception &error) {
    std::cerr << error.what() << '\n';
    return 1;
  }
}
