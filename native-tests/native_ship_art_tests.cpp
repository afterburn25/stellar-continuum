#include "native_fleet_route_effects.hpp"
#include "native_fleet_workspace.hpp"
#include "native_ship_art_assets.hpp"
#include "native_shipyard_workspace.hpp"

#include <algorithm>
#include <filesystem>
#include <iostream>
#include <ranges>
#include <stdexcept>
#include <string>
#include <string_view>
#include <unordered_map>
#include <variant>
#include <vector>

namespace {
using namespace stellar::core;
using namespace stellar::native_fleet;
using namespace stellar::native_fleet_ui;
using namespace stellar::native_map;
using namespace stellar::native_ship_ui;
using namespace stellar::native_shipyard;
using namespace stellar::native_shipyard_ui;

void require(bool condition, std::string_view expression, int line) {
  if (!condition)
    throw std::runtime_error("Ship art check failed at line " +
                             std::to_string(line) + ": " +
                             std::string(expression));
}

#define REQUIRE(expression) require((expression), #expression, __LINE__)

std::size_t overlay_images(const DrawList &out) {
  return static_cast<std::size_t>(std::ranges::count_if(
      out.overlay,
      [](const auto &value) { return std::holds_alternative<Image>(value); }));
}

void artwork_resolution() {
  REQUIRE(ship_artwork_for("warp_scout", FleetRole::Military) ==
          ShipArtwork::pathfinder_scout);
  REQUIRE(ship_artwork_for("science_vessel", FleetRole::Scout) ==
          ShipArtwork::science_vessel);
  REQUIRE(ship_artwork_for("patrol_corvette", FleetRole::Scout) ==
          ShipArtwork::patrol_corvette);
  REQUIRE(ship_artwork_for("colony_ship", FleetRole::Scout) ==
          ShipArtwork::colony_ship);
  REQUIRE(ship_artwork_for("resource_outpost_ship", FleetRole::Scout) ==
          ShipArtwork::resource_outpost_ship);
  REQUIRE(ship_artwork_for("bulk_freighter", FleetRole::Scout) ==
          ShipArtwork::bulk_freighter);
  // Unregistered and absent design identifiers fall back to the role baseline
  // exactly like the preserved ShipArtworkLibrary.
  REQUIRE(ship_artwork_for("unregistered_design", FleetRole::Science) ==
          ShipArtwork::science_vessel);
  REQUIRE(ship_artwork_for(std::nullopt, FleetRole::Scout) ==
          ShipArtwork::pathfinder_scout);
  REQUIRE(ship_artwork_for(std::nullopt, FleetRole::Science) ==
          ShipArtwork::science_vessel);
  REQUIRE(ship_artwork_for(std::nullopt, FleetRole::Military) ==
          ShipArtwork::patrol_corvette);
  REQUIRE(ship_artwork_for(std::nullopt, FleetRole::Colony) ==
          ShipArtwork::colony_ship);
  REQUIRE(ship_artwork_for(std::nullopt, FleetRole::Logistics) ==
          ShipArtwork::bulk_freighter);
}

void bounded_thumbnail_cache(const std::filesystem::path &root) {
  NativeShipArtAssets assets(root);
  std::vector<std::shared_ptr<const RgbaImage>> images;
  for (const auto artwork :
       {ShipArtwork::pathfinder_scout, ShipArtwork::science_vessel,
        ShipArtwork::patrol_corvette, ShipArtwork::colony_ship,
        ShipArtwork::resource_outpost_ship, ShipArtwork::bulk_freighter})
    images.push_back(assets.image(artwork));
  REQUIRE(assets.cached_count() == 6 && assets.decoded_count() == 6);
  REQUIRE(assets.cached_count() <= maximum_ship_art_entries);
  REQUIRE(assets.cache_bytes() <= maximum_ship_art_bytes);
  for (const auto &image : images) {
    REQUIRE(image->width() == ship_art_pixels &&
            image->height() == ship_art_pixels);
    // Approved portraits must survive downsampling with visible varied
    // content rather than a flat placeholder fill.
    const auto &pixels = image->pixels();
    REQUIRE(!pixels.empty());
    const auto channel_minmax = std::ranges::minmax_element(
        pixels | std::views::stride(4));
    REQUIRE(*channel_minmax.min != *channel_minmax.max);
  }
  const auto again = assets.image(ShipArtwork::pathfinder_scout);
  REQUIRE(again == images.front() && assets.decoded_count() == 6);
}

NativeFleetMapView fleet_view() {
  NativeFleetMapView view;
  view.campaign_generation = 3;
  view.player_civilization_id = 7;
  NativeOwnFleet scout{.id = 1,
                       .name = "Pathfinder One",
                       .role = FleetRole::Scout,
                       .design_id = "warp_scout",
                       .position = {0., 0.}};
  NativeOwnFleet colony{.id = 2,
                        .name = "Settlement Two",
                        .role = FleetRole::Colony,
                        .position = {10., 0.}};
  view.own_fleets = {std::move(scout), std::move(colony)};
  view.selected_fleet_id = 1;
  return view;
}

void fleet_workspace_thumbnails(const std::filesystem::path &root) {
  NativeShipArtAssets assets(root);
  NativeFleetWorkspace workspace;
  workspace.set_view(fleet_view());
  DrawList out;
  workspace.render(out, 1280, 720, {}, &assets);
  // Two fleet rows plus the selected-fleet details portrait.
  REQUIRE(workspace.last_ship_art_rows() == 2);
  REQUIRE(overlay_images(out) >= 3);
  DrawList without_art;
  workspace.render(without_art, 1280, 720, {});
  REQUIRE(workspace.last_ship_art_rows() == 0);
  REQUIRE(overlay_images(without_art) == 0);
  NativeFleetWorkspace commands{FleetWorkspacePresentation::SelectedCommands};
  commands.set_view(fleet_view());
  DrawList selected;
  commands.render(selected, 1280, 720, {}, &assets);
  REQUIRE(commands.last_ship_art_rows() == 1);
  REQUIRE(overlay_images(selected) == 1);
  auto unselected = fleet_view();
  unselected.selected_fleet_id.reset();
  commands.set_view(std::move(unselected));
  DrawList hidden;
  commands.render(hidden, 1280, 720, {}, &assets);
  REQUIRE(commands.last_ship_art_rows() == 0 && overlay_images(hidden) == 0);
}

NativeShipyardView shipyard_view() {
  NativeShipyardView view;
  view.campaign_generation = 3;
  view.shipyard_revision = 4;
  view.player_civilization_id = 7;
  view.home_system_id = 1;
  view.orbital_shipyard_complete = true;
  view.currency = {.name = "Solar",
                   .code = "SOL",
                   .symbol = "$",
                   .local_units_per_budget_unit = 1.};
  view.formatted_treasury = "$90 SOL";
  view.maximum_pending_builds = 4;
  view.available_designs = {
      {.id = "warp_scout",
       .name = "Pathfinder Scout",
       .description = "Survey design.",
       .role = FleetRole::Scout,
       .formatted_credit_cost = "$12 SOL",
       .can_start = true},
      {.id = "bulk_freighter",
       .name = "Interstellar Bulk Freighter",
       .description = "Freight design.",
       .role = FleetRole::Logistics,
       .formatted_credit_cost = "$30 SOL",
       .can_start = true}};
  return view;
}

void shipyard_workspace_thumbnails(const std::filesystem::path &root) {
  NativeShipArtAssets assets(root);
  NativeShipyardWorkspace workspace;
  workspace.open();
  workspace.set_view(shipyard_view());
  DrawList out;
  workspace.render(out, 1280, 720, &assets);
  // Two design rows plus the selected design's details artwork.
  REQUIRE(workspace.last_ship_art_rows() == 2);
  REQUIRE(overlay_images(out) >= 3);
}

std::unordered_map<int, const StellarSystem *> route_cache(
    const std::vector<StellarSystem> &systems) {
  std::unordered_map<int, const StellarSystem *> result;
  for (const auto &system : systems) result.emplace(system.id, &system);
  return result;
}

void route_effects() {
  std::vector<StellarSystem> systems(3);
  systems[0].id = 10;
  systems[0].position = {100., 0.};
  systems[1].id = 20;
  systems[1].position = {200., 0.};
  systems[2].id = 30;
  systems[2].position = {300., 0.};
  const auto cache = route_cache(systems);
  const Camera camera{{}, 1.};
  NativeOwnFleet fleet{.id = 1,
                       .name = "Runner",
                       .role = FleetRole::Scout,
                       .position = {0., 0.},
                       .destination_system_id = 30,
                       .transit_phase = FleetTransitPhase::InterstellarWarp,
                       .planned_route_system_ids = {10, 20, 30}};

  // Every planned leg draws strokes, dashes, chevrons and the current-leg
  // trail plus progress circle; own-fleet courses are the player's mission
  // data and every endpoint is already a public map marker.
  DrawList full;
  const auto full_stats = append_fleet_route_effects(
      full, {&fleet, 1}, cache, camera, 640, 480);
  REQUIRE(!full.lines.empty() && full.circles.size() == 1);
  REQUIRE(full_stats.routed_fleets == 1 && full_stats.drawn_legs == 3 &&
          full_stats.chevron_segments == 3 &&
          full_stats.trail_strokes == 3 &&
          full_stats.position_circles == 1 &&
          full_stats.emitted_lines == static_cast<int>(full.lines.size()));
  REQUIRE(std::ranges::all_of(full.circles, [](const Circle &circle) {
    return circle.radius > 0.f;
  }));

  // A route leg referencing a system absent from the catalog truncates the
  // chain at the first missing endpoint.
  NativeOwnFleet broken = fleet;
  broken.planned_route_system_ids = {10, 99, 30};
  DrawList truncated;
  const auto truncated_stats = append_fleet_route_effects(
      truncated, {&broken, 1}, cache, camera, 640, 480);
  REQUIRE(truncated.circles.size() == 1);
  REQUIRE(truncated_stats.drawn_legs == 1 &&
          truncated_stats.emitted_lines ==
              static_cast<int>(truncated.lines.size()));
  REQUIRE(truncated.lines.size() < full.lines.size());
  REQUIRE(std::ranges::none_of(truncated.lines, [](const Line &line) {
    return line.to.x > 500.f;
  }));

  // No destination means no route presentation at all.
  NativeOwnFleet idle = fleet;
  idle.destination_system_id.reset();
  idle.planned_route_system_ids.clear();
  DrawList empty;
  append_fleet_route_effects(empty, {&idle, 1}, cache, camera, 640, 480);
  REQUIRE(empty.lines.empty() && empty.circles.empty());

  // A fleet mid-leg with no stored planned route falls back to a single leg
  // toward its destination, matching the preserved map.
  NativeOwnFleet direct = fleet;
  direct.planned_route_system_ids.clear();
  DrawList fallback;
  append_fleet_route_effects(fallback, {&direct, 1}, cache, camera, 640, 480);
  REQUIRE(!fallback.lines.empty() && fallback.circles.size() == 1);
}
} // namespace

int main(int argc, char **argv) {
  try {
    if (argc != 2)
      throw std::invalid_argument("usage: test_native_ship_art <asset-root>");
    const std::filesystem::path root(argv[1]);
    artwork_resolution();
    bounded_thumbnail_cache(root);
    fleet_workspace_thumbnails(root);
    shipyard_workspace_thumbnails(root);
    route_effects();
    std::cout << "native ship art: 5/5 cases passed\n";
    return 0;
  } catch (const std::exception &error) {
    std::cerr << "native ship art failure: " << error.what() << '\n';
    return 1;
  }
}
