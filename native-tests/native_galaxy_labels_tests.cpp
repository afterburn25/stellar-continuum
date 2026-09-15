#include "native_galaxy_labels.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <iostream>
#include <limits>
#include <random>
#include <stdexcept>
#include <string>
#include <vector>

using namespace stellar::native_galaxy_ui;
using namespace stellar::native_map;

namespace {

void require(bool value, const std::string &message) {
  if (!value) throw std::runtime_error(message);
}

NativeGalaxyLabelCandidate system(int id, Point anchor, std::string name,
                                  bool selected = false,
                                  double priority = 0.) {
  return {NativeGalaxyLabelKind::system, id, anchor, 7.f,
          Text{{}, std::move(name), {205, 222, 245, 235}, 13}, selected,
          priority};
}

NativeGalaxyLabelCandidate empire(int id, Point anchor, std::string name,
                                  double priority = 0.) {
  return {NativeGalaxyLabelKind::empire, id, anchor, 0.f,
          Text{{}, std::move(name), {126, 210, 245, 220}, 13}, false,
          priority};
}

TextExtent variable_measure(const Text &label) {
  require(label.font_pixel_size == 13,
          "layout changed the font used by the renderer");
  require(label.wrap_width == 0.f,
          "layout introduced a render-only wrap width");
  return {static_cast<int>(label.value.size()) * 6 +
              (label.value.starts_with("W") ? 9 : 0),
          17};
}

bool same_placements(const NativeGalaxyLabelLayout &left,
                     const NativeGalaxyLabelLayout &right) {
  if (left.placements.size() != right.placements.size()) return false;
  for (std::size_t index = 0; index < left.placements.size(); ++index) {
    const auto &a = left.placements[index];
    const auto &b = right.placements[index];
    if (a.kind != b.kind || a.stable_id != b.stable_id ||
        a.label.value != b.label.value || a.bounds.x != b.bounds.x ||
        a.bounds.y != b.bounds.y || a.bounds.width != b.bounds.width ||
        a.bounds.height != b.bounds.height)
      return false;
  }
  return true;
}

} // namespace

int main() try {
  const UiRect viewport{0, 0, 640, 360};
  const std::vector<NativeGalaxyLabelObstacle> basic_obstacles{
      {{0, 0, 640, 48}, NativeGalaxyLabelObstacleKind::hud},
      {{313, 173, 14, 14}, NativeGalaxyLabelObstacleKind::star}};

  auto basic = layout_native_galaxy_labels(
      {system(1, {320, 180}, "Wide system", true, 100.),
       system(2, {350, 180}, "Neighbor", false, 90.),
       empire(5, {320, 220}, "KNOWN EMPIRE", 2.)},
      viewport, basic_obstacles, variable_measure);
  require(basic.stats.candidates == 3 && basic.stats.measured == 3,
          "basic layout did not measure its bounded candidate set");
  require(basic.stats.selected_requested == 1 &&
              basic.stats.selected_placed == 1,
          "selected system label was not favored");
  require(basic.stats.label_overlaps == 0 &&
              basic.stats.obstacle_overlaps == 0 &&
              basic.stats.outside_viewport == 0,
          "basic layout emitted colliding or clipped labels");
  require(std::ranges::any_of(basic.placements, [](const auto &placement) {
            return placement.kind == NativeGalaxyLabelKind::empire;
          }),
          "empire label did not share collision resolution with system labels");

  std::vector<NativeGalaxyLabelCandidate> crowded_home{
      system(100, {500, 180}, "Home", true, 10'000.),
      empire(7, {320, 180}, "HUMAN COMMONWEALTH", 5.)};
  for (int id = 0; id < 18; ++id)
    crowded_home.push_back(system(
        200 + id,
        {320.f + static_cast<float>((id % 3) - 1) * 6.f,
         180.f + static_cast<float>((id % 2) * 2)},
        "Known " + std::to_string(id), false, 9'000. - id));
  std::vector<NativeGalaxyLabelObstacle> home_stars;
  for (int y = 100; y < 260; y += 14)
    for (int x = 180; x < 460; x += 14)
      home_stars.push_back(
          {{static_cast<float>(x), static_cast<float>(y), 14, 14},
           NativeGalaxyLabelObstacleKind::star});
  home_stars.push_back(
      {{493, 173, 14, 14}, NativeGalaxyLabelObstacleKind::star});
  const auto home_layout = layout_native_galaxy_labels(
      crowded_home, viewport, home_stars, variable_measure);
  require(home_layout.placements.size() >= 2 &&
              home_layout.placements[0].selected &&
              home_layout.placements[1].kind ==
                  NativeGalaxyLabelKind::empire,
          "crowded home region did not preserve selected-then-empire priority");
  const auto &empire_bounds = home_layout.placements[1].bounds;
  require(home_layout.placements[1].anchor.x == 320.f &&
              home_layout.placements[1].anchor.y == 180.f,
          "crowded empire placement did not retain its territory anchor");
  const Point empire_center{empire_bounds.x + empire_bounds.width * .5f,
                            empire_bounds.y + empire_bounds.height * .5f};
  require(std::hypot(empire_center.x - 320.f, empire_center.y - 180.f) >
              100.f,
          "720p cluster did not force the empire label beyond 48 pixels");
  require(home_layout.stats.label_overlaps == 0 &&
              home_layout.stats.star_overlaps == 0 &&
              home_layout.stats.outside_viewport == 0,
          "crowded home labels did not retain collision guarantees");

  std::vector<NativeGalaxyLabelObstacle> cardinal_blockers;
  constexpr std::array<float, 3> blocked_distances{20.f, 48.f, 76.f};
  for (const float distance : blocked_distances) {
    cardinal_blockers.push_back(
        {{315, 180 - distance - 18, 10, 18},
         NativeGalaxyLabelObstacleKind::star});
    cardinal_blockers.push_back(
        {{315, 180 + distance, 10, 18},
         NativeGalaxyLabelObstacleKind::star});
    cardinal_blockers.push_back(
        {{320 + distance, 175, 10, 10},
         NativeGalaxyLabelObstacleKind::star});
    cardinal_blockers.push_back(
        {{320 - distance - 10, 175, 10, 10},
         NativeGalaxyLabelObstacleKind::star});
  }
  const auto nearest_option = layout_native_galaxy_labels(
      {empire(9, {320, 180}, "EMPIRE")}, viewport, cardinal_blockers,
      variable_measure);
  require(nearest_option.placements.size() == 1,
          "nearest-option fixture did not place its empire label");
  const auto &nearest_bounds = nearest_option.placements.front().bounds;
  const Point nearest_center{nearest_bounds.x + nearest_bounds.width * .5f,
                             nearest_bounds.y + nearest_bounds.height * .5f};
  require(std::hypot(nearest_center.x - 320.f,
                     nearest_center.y - 180.f) < 60.f,
          "layout chose a farther cardinal before a nearer diagonal");

  std::vector<NativeGalaxyLabelCandidate> crowded;
  for (int id = 0; id < 80; ++id)
    crowded.push_back(system(id, {300.f + static_cast<float>(id % 3),
                                  180.f + static_cast<float>(id % 2)},
                              "Crowded " + std::to_string(id), id == 73,
                              1000. - id));
  const std::vector<NativeGalaxyLabelObstacle> crowded_obstacles{
      {{0, 0, 640, 150}, NativeGalaxyLabelObstacleKind::hud},
      {{0, 210, 640, 150}, NativeGalaxyLabelObstacleKind::hud},
      {{293, 173, 14, 14}, NativeGalaxyLabelObstacleKind::star}};
  const auto crowded_layout = layout_native_galaxy_labels(
      crowded, viewport, crowded_obstacles, variable_measure);
  require(crowded_layout.stats.selected_requested == 1 &&
              crowded_layout.stats.selected_placed == 1,
          "crowded cluster omitted its selected label");
  require(crowded_layout.placements.size() < crowded.size(),
          "crowded cluster did not omit labels without clean placements");
  require(crowded_layout.stats.hud_overlaps == 0 &&
              crowded_layout.stats.star_overlaps == 0,
          "crowded cluster crossed a HUD or star footprint");

  auto edge_layout = layout_native_galaxy_labels(
      {system(10, {2, 2}, "Edge"), system(11, {620, 340}, "Bottom edge")},
      viewport, {}, variable_measure);
  require(edge_layout.stats.outside_viewport == 0,
          "edge placement escaped the viewport");
  for (const auto &placement : edge_layout.placements)
    require(placement.label.at.x == placement.bounds.x &&
                placement.label.at.y == placement.bounds.y &&
                placement.label.font_pixel_size == 13,
            "placed render text diverged from measured bounds");

  int measurements = 0;
  std::vector<NativeGalaxyLabelCandidate> large;
  large.reserve(2500);
  for (int id = 0; id < 2470; ++id)
    large.push_back(system(id, {100.f + static_cast<float>(id % 400),
                                80.f + static_cast<float>(id % 200)},
                           "System " + std::to_string(id), false,
                           -static_cast<double>(id)));
  for (int id = 0; id < 30; ++id)
    large.push_back(empire(id, {180.f + id * 5.f, 260.f},
                           "Empire " + std::to_string(id), id));
  const auto bounded = layout_native_galaxy_labels(
      large, viewport, {}, [&](const Text &label) {
        ++measurements;
        return variable_measure(label);
      });
  require(bounded.stats.candidates == 2500,
          "large layout lost its pre-budget candidate count");
  require(measurements == static_cast<int>(maximum_measured_labels) &&
              bounded.stats.measured == maximum_measured_labels,
          "large layout exceeded its 128-entry text measurement budget");
  require(std::ranges::any_of(bounded.placements, [](const auto &placement) {
            return placement.kind == NativeGalaxyLabelKind::empire;
          }),
          "measurement budget starved all empire labels");

  auto shuffled = large;
  std::mt19937 random(42);
  std::shuffle(shuffled.begin(), shuffled.end(), random);
  const auto first =
      layout_native_galaxy_labels(large, viewport, {}, variable_measure);
  const auto second =
      layout_native_galaxy_labels(shuffled, viewport, {}, variable_measure);
  require(same_placements(first, second),
          "layout depended on candidate input order");

  const auto invalid_metrics = layout_native_galaxy_labels(
      {system(20, {200, 200}, "zero"), system(21, {300, 200}, "negative")},
      viewport, {}, [](const Text &label) {
        return label.value == "zero" ? TextExtent{0, 17}
                                     : TextExtent{40, -1};
      });
  require(invalid_metrics.placements.empty() &&
              invalid_metrics.stats.measured == 2,
          "invalid renderer metrics produced a placement");

  bool rejected_viewport = false;
  try {
    (void)layout_native_galaxy_labels(
        {}, {0, 0, std::numeric_limits<float>::infinity(), 10}, {},
        variable_measure);
  } catch (const std::invalid_argument &) {
    rejected_viewport = true;
  }
  require(rejected_viewport, "non-finite viewport was accepted");

  bool rejected_huge_viewport = false;
  try {
    (void)layout_native_galaxy_labels(
        {}, {0, 0, std::numeric_limits<float>::max(), 10}, {},
        variable_measure);
  } catch (const std::invalid_argument &) {
    rejected_huge_viewport = true;
  }
  require(rejected_huge_viewport,
          "viewport unsafe for spatial-grid integer conversion was accepted");

  bool rejected_obstacle = false;
  try {
    (void)layout_native_galaxy_labels(
        {}, viewport,
        {{{0, 0, -1, 10}, NativeGalaxyLabelObstacleKind::hud}},
        variable_measure);
  } catch (const std::invalid_argument &) {
    rejected_obstacle = true;
  }
  require(rejected_obstacle, "non-positive obstacle was accepted");

  int filtered_measurements = 0;
  const auto filtered = layout_native_galaxy_labels(
      {system(30, {-50, 100}, "offscreen"),
       system(31, {100, 100}, "nan", false,
              std::numeric_limits<double>::quiet_NaN()),
       system(32, {120, 120}, "valid")},
      viewport, {}, [&](const Text &label) {
        ++filtered_measurements;
        return variable_measure(label);
      });
  require(filtered.stats.candidates == 1 && filtered_measurements == 1 &&
              filtered.placements.size() == 1 &&
              filtered.placements.front().stable_id == 32,
          "invalid-priority or offscreen candidates consumed measurement budget");

  std::vector<NativeGalaxyLabelPlacement> bad_placements{
      {NativeGalaxyLabelKind::system, 1, false, {}, Text{}, {10, 10, 30, 16}},
      {NativeGalaxyLabelKind::empire, 2, false, {}, Text{}, {20, 12, 30, 16}},
      {NativeGalaxyLabelKind::system, 3, false, {}, Text{},
       {630, 350, 30, 16}}};
  const auto audit = inspect_native_galaxy_labels(
      bad_placements, viewport,
      {{{5, 5, 14, 20}, NativeGalaxyLabelObstacleKind::hud},
       {{35, 10, 10, 10}, NativeGalaxyLabelObstacleKind::star}});
  require(audit.label_overlaps == 1 && audit.outside_viewport == 1 &&
              audit.obstacle_overlaps == 2 && audit.hud_overlaps == 1 &&
              audit.star_overlaps == 2,
          "diagnostic audit did not derive collisions from final bounds");

  std::cout << "native galaxy label tests passed\n";
  return 0;
} catch (const std::exception &error) {
  std::cerr << error.what() << '\n';
  return 1;
}
