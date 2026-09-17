#pragma once

#include "native_fleet_controller.hpp"

#include <stellar/core/massive_combat_observer.hpp>
#include <stellar/core/massive_combat_persistence.hpp>
#include <stellar/engine/native_map_platform.hpp>

#include <cstddef>
#include <cstdint>
#include <functional>
#include <string>
#include <vector>

namespace stellar::native_battle_art {

inline constexpr std::size_t maximum_battle_art_sprites = 32;

struct BattleArtBinding {
  std::int64_t formation_id{};
  std::int64_t vessel_id{};
  std::string design_id;
  stellar::core::MassivePoint position{};
  stellar::core::MassivePoint velocity{};
  float heading_radians{};
  std::uint16_t vessel_index{};
};

struct BattleArtSprite {
  std::int64_t formation_id{};
  std::int64_t vessel_id{};
  stellar::native_map::Point center{};
  stellar::native_map::Point size{};
  float heading_degrees{};
  bool moving{};
  stellar::native_map::UiRect clip{};
};

using BattleArtProjection = std::function<stellar::native_map::Point(
    stellar::core::MassivePoint)>;

[[nodiscard]] std::vector<BattleArtBinding> bind_owned_battle_art(
    const stellar::core::MassiveCombatSnapshot &snapshot,
    const stellar::core::CampaignMassiveEncounter &encounter, int observer,
    const stellar::native_fleet::NativeFleetMapView &fleet_view);

[[nodiscard]] std::vector<BattleArtSprite> prepare_battle_art(
    const std::vector<BattleArtBinding> &bindings,
    const BattleArtProjection &projection, stellar::native_map::UiRect clip,
    float pixels_per_world, float ui_scale,
    std::size_t budget = maximum_battle_art_sprites);

} // namespace stellar::native_battle_art
