#include "native_battle_art.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <numbers>
#include <optional>
#include <ranges>
#include <set>
#include <tuple>

namespace stellar::native_battle_art {
namespace {

constexpr float maximum_magnitude = 10'000'000.F;
constexpr float moving_epsilon = .001F;

bool finite_bounded(float value) noexcept {
  return std::isfinite(value) && std::abs(value) <= maximum_magnitude;
}

bool valid_point(stellar::core::MassivePoint value) noexcept {
  return finite_bounded(value.x) && finite_bounded(value.y);
}

bool valid_point(stellar::native_map::Point value) noexcept {
  return finite_bounded(value.x) && finite_bounded(value.y);
}

bool valid_clip(stellar::native_map::UiRect value) noexcept {
  return finite_bounded(value.x) && finite_bounded(value.y) &&
         std::isfinite(value.width) && std::isfinite(value.height) &&
         value.width > 0 && value.height > 0 &&
         value.width <= maximum_magnitude && value.height <= maximum_magnitude &&
         finite_bounded(value.x + value.width) &&
         finite_bounded(value.y + value.height);
}

std::optional<stellar::native_map::UiRect>
intersection(stellar::native_map::UiRect first,
             stellar::native_map::UiRect second) noexcept {
  const auto left = std::max(first.x, second.x);
  const auto top = std::max(first.y, second.y);
  const auto right = std::min(first.x + first.width, second.x + second.width);
  const auto bottom = std::min(first.y + first.height, second.y + second.height);
  if (!(right > left && bottom > top))
    return std::nullopt;
  return stellar::native_map::UiRect{left, top, right - left, bottom - top};
}

stellar::native_map::Point vessel_offset(std::uint16_t index,
                                         float spacing) noexcept {
  constexpr std::array directions{
      stellar::native_map::Point{1, 0},  stellar::native_map::Point{-1, 0},
      stellar::native_map::Point{0, 1},  stellar::native_map::Point{0, -1},
      stellar::native_map::Point{1, 1},  stellar::native_map::Point{-1, 1},
      stellar::native_map::Point{1, -1}, stellar::native_map::Point{-1, -1}};
  const auto offset = static_cast<std::size_t>(index);
  const auto ring = static_cast<float>(offset / directions.size() + 1);
  const auto direction = directions[offset % directions.size()];
  return {direction.x * spacing * ring, direction.y * spacing * ring};
}

} // namespace

std::vector<BattleArtBinding> bind_owned_battle_art(
    const stellar::core::MassiveCombatSnapshot &snapshot,
    const stellar::core::CampaignMassiveEncounter &encounter,
    const int observer,
    const stellar::native_fleet::NativeFleetMapView &fleet_view) {
  if (encounter.reconciled || observer != fleet_view.player_civilization_id ||
      observer < 0 || snapshot.battle_id != encounter.battle.battle_id ||
      std::ranges::none_of(snapshot.battle_id,
                           [](std::uint8_t value) { return value != 0; }))
    return {};

  std::set<int> fleet_ids;
  for (const auto &fleet : fleet_view.own_fleets)
    if (!fleet_ids.insert(fleet.id).second)
      return {};

  std::set<int> bound_fleet_ids;
  for (const auto &binding : encounter.vessels)
    if (!bound_fleet_ids.insert(binding.fleet_id).second)
      return {};

  std::set<std::int64_t> formation_ids;
  std::set<std::int64_t> vessel_ids;
  for (const auto &formation : snapshot.formations) {
    if (!formation_ids.insert(formation.formation_id).second)
      return {};
    for (const auto &vessel : formation.important_vessels)
      if (!vessel_ids.insert(vessel.vessel_id).second)
        return {};
  }

  std::vector<BattleArtBinding> result;
  result.reserve(std::min(encounter.vessels.size(), maximum_battle_art_sprites));
  for (const auto &binding : encounter.vessels) {
    const auto fleet = std::ranges::find(fleet_view.own_fleets,
                                         binding.fleet_id,
                                         &stellar::native_fleet::NativeOwnFleet::id);
    if (fleet == fleet_view.own_fleets.end() || !fleet->design_id ||
        *fleet->design_id != "patrol_corvette")
      continue;
    const auto formation = std::ranges::find(
        snapshot.formations, binding.formation_id,
        &stellar::core::MassiveObservedFormation::formation_id);
    if (formation == snapshot.formations.end() || !formation->is_exact ||
        formation->civilization_id != observer ||
        !valid_point(formation->position))
      continue;
    const auto vessel_id =
        stellar::core::campaign_vessel_id_for_fleet(fleet->id);
    const auto vessel = std::ranges::find(
        formation->important_vessels, vessel_id,
        &stellar::core::MassiveObservedVessel::vessel_id);
    if (vessel == formation->important_vessels.end() ||
        vessel->design_id != *fleet->design_id)
      continue;
    const auto index = static_cast<std::size_t>(
        std::distance(formation->important_vessels.begin(), vessel));
    if (index >= maximum_battle_art_sprites)
      continue;
    if (result.size() < maximum_battle_art_sprites)
      result.push_back({formation->formation_id, vessel_id, *fleet->design_id,
                        formation->position, formation->velocity,
                        formation->heading_radians,
                        static_cast<std::uint16_t>(index)});
  }
  std::ranges::sort(result, [](const BattleArtBinding &left,
                              const BattleArtBinding &right) {
    return std::tie(left.formation_id, left.vessel_id) <
           std::tie(right.formation_id, right.vessel_id);
  });
  return result;
}

std::vector<BattleArtSprite> prepare_battle_art(
    const std::vector<BattleArtBinding> &bindings,
    const BattleArtProjection &projection,
    const stellar::native_map::UiRect clip, const float pixels_per_world,
    const float ui_scale, const std::size_t budget) {
  if (!projection || !valid_clip(clip) || !std::isfinite(pixels_per_world) ||
      pixels_per_world <= 0 || pixels_per_world > 10'000.F ||
      !std::isfinite(ui_scale) || ui_scale < .25F || ui_scale > 8.F)
    return {};

  const auto limit =
      std::min({bindings.size(), budget, maximum_battle_art_sprites});
  std::vector<BattleArtSprite> result;
  result.reserve(limit);
  for (std::size_t index = 0; index < bindings.size() && result.size() < limit;
       ++index) {
    const auto &binding = bindings[index];
    if (binding.formation_id <= 0 || binding.vessel_id <= 0 ||
        binding.design_id != "patrol_corvette" ||
        !valid_point(binding.position))
      continue;
    auto center = projection(binding.position);
    if (!valid_point(center))
      continue;

    const auto extent = std::min(
        320.F, std::clamp(64.F * std::sqrt(pixels_per_world) * ui_scale,
                          48.F * ui_scale, 180.F * ui_scale));
    if (binding.vessel_index >= maximum_battle_art_sprites)
      continue;
    const auto offset = vessel_offset(binding.vessel_index, extent * .75F);
    center.x += offset.x;
    center.y += offset.y;
    if (!valid_point(center))
      continue;

    const auto velocity_valid = valid_point(binding.velocity);
    const auto speed = velocity_valid
                           ? std::hypot(binding.velocity.x, binding.velocity.y)
                           : 0.F;
    const auto moving = std::isfinite(speed) && speed > moving_epsilon;
    auto heading = moving ? std::atan2(binding.velocity.y, binding.velocity.x)
                          : binding.heading_radians;
    if (!std::isfinite(heading))
      heading = 0;
    auto degrees = std::remainder(
        heading * 180.F / std::numbers::pi_v<float>, 360.F);
    if (!std::isfinite(degrees))
      degrees = 0;
    const auto visibility_radius = extent * .75F;
    const stellar::native_map::UiRect bounds{
        center.x - visibility_radius, center.y - visibility_radius,
        visibility_radius * 2, visibility_radius * 2};
    const auto visible = intersection(bounds, clip);
    if (!visible)
      continue;
    result.push_back({binding.formation_id, binding.vessel_id, center,
                      {extent, extent}, degrees, moving, clip});
  }
  return result;
}

} // namespace stellar::native_battle_art
