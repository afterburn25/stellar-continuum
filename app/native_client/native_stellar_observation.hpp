#pragma once
#include "native_ui_layout.hpp"
#include <stellar/core/fresh_campaign.hpp>
#include <algorithm>
#include <optional>
#include <string_view>

namespace stellar::native_stellar {
struct ObservedStellarArtwork {
  core::StellarObjectType type;
  std::string_view id;
  float scale;
};
// Older campaigns and companion stars may have a surveyed spectral class but
// no physical record. Resolve their artwork without inventing simulation data.
inline std::optional<ObservedStellarArtwork> observed_stellar_artwork(
    core::SystemSurveyLevel survey,
    const std::optional<core::StellarPhysicalProperties>& physics,
    std::optional<core::StellarClass> spectral_class) {
  if(survey!=core::SystemSurveyLevel::fully_surveyed)return std::nullopt;
  std::optional<core::StellarObjectType> type;
  if(physics)type=physics->type;
  else if(spectral_class){
    using enum core::StellarClass;
    using T=core::StellarObjectType;
    switch(*spectral_class){
    case MRedDwarf:type=T::MRedDwarf;break;
    case KOrangeDwarf:type=T::KOrangeStar;break;
    case GYellowDwarf:type=T::GYellowStar;break;
    case FYellowWhiteDwarf:type=T::FYellowWhiteStar;break;
    case AWhiteStar:type=T::AWhiteStar;break;
    case HotBlueStar:type=T::BBlueWhiteStar;break;
    case Giant:type=T::RedGiant;break;
    case WhiteDwarf:type=T::WhiteDwarf;break;
    case NeutronStar:type=T::QuietNeutronStar;break;
    case BlackHole:type=T::QuiescentBlackHole;break;
    case Pulsar:type=T::Pulsar;break;
    case Protostar:break; // No supplied protostar artwork.
    }
  }
  if(!type)return std::nullopt;
  const auto& definition=core::stellar_object_definition(*type);
  const std::string_view id=*type==core::StellarObjectType::MRedDwarf&&(!physics||!physics->active)
      ?std::string_view{"m-red-dwarf-quiet"}:std::string_view{definition.id};
  return ObservedStellarArtwork{*type,id,static_cast<float>(definition.visual_scale)};
}
// Keep the central object prominent at overview and bounded at close zoom.
// This presentation scale never changes its authoritative mass or physics.
inline float central_black_hole_map_radius(
    double exclusion_radius, double pixels_per_world, int width, int height) {
  const double ui_scale = std::clamp(height / 1080. *
      static_cast<double>(native_map::NativeUiLayout::user_scale()), .67, 2.);
  const double maximum = .34 * std::min(width, height);
  return static_cast<float>(std::clamp(exclusion_radius * pixels_per_world * .9,
      std::min(55. * ui_scale, maximum), maximum));
}

// Resolve presentation only after the observer has explored the central object.
// Developer inspection/state changes do not bypass this knowledge boundary.
inline std::optional<std::string_view> observed_central_artwork(
    const core::FreshCampaignState &world, int observer) {
  if (!world.core || !world.galactic_core || !world.galactic_core->black_hole ||
      !world.knowledge.is_galactic_core_discovered(observer))
    return std::nullopt;
  switch (world.galactic_core->black_hole->state) {
  case core::CentralBlackHoleState::Quiescent:
    return "central-supermassive-black-hole";
  case core::CentralBlackHoleState::Accreting:
    return "accreting-black-hole";
  case core::CentralBlackHoleState::RelativisticJets:
    return "jet-black-hole";
  }
  return std::nullopt;
}
}
