#pragma once

#include <stellar/core/construction_projects.hpp>
#include <stellar/core/galaxy_phenomena.hpp>
#include <stellar/core/planet_appearance.hpp>
#include <stellar/core/ship_designs.hpp>
#include <stellar/core/species_environment.hpp>
#include <stellar/core/stellar_object.hpp>
#include <stellar/core/surface_economy.hpp>
#include <stellar/engine/localization.hpp>

#include <algorithm>
#include <cctype>
#include <string>
#include <string_view>

namespace stellar::native_data {

// Data-authored display names (stellar object classes, planet class/subclass
// labels, species profiles) are authored in English data files and stay
// authoritative. Display boundaries resolve them through the locale table
// keyed on the stable data id; an absent key returns the authored English
// name unchanged.
[[nodiscard]] inline std::string key_of(std::string_view prefix,
                                        std::string_view id) {
  std::string key{prefix};
  for (const char c : id)
    key += (c == '-' || c == '_')
               ? '_'
               : static_cast<char>(
                     std::toupper(static_cast<unsigned char>(c)));
  return key;
}

[[nodiscard]] inline std::string data_name(
    const stellar::engine::LocalizationTable *locale, std::string_view key,
    std::string_view authored_name) {
  return locale && locale->contains(key)
             ? std::string(locale->translate(key))
             : std::string(authored_name);
}

[[nodiscard]] inline std::string stellar_object_name(
    const stellar::engine::LocalizationTable *locale,
    const stellar::core::StellarObjectDefinition &definition) {
  return data_name(locale, key_of("DATA_STELLAR_", definition.id),
                   definition.name);
}

[[nodiscard]] inline std::string planet_appearance_name(
    const stellar::engine::LocalizationTable *locale,
    const stellar::core::PlanetAppearance &appearance) {
  // Mirrors planet_appearance_display_name: physical-fallback and authored Sol
  // assets name the class; anything else names the registered subclass (an
  // unknown subclass throws, preserving the authoritative contract).
  if (appearance.subclass == "physical-fallback" ||
      appearance.source_asset_id.starts_with("sol:")) {
    const auto &definition =
        stellar::core::planet_class_definition(appearance.primary_class);
    return data_name(locale, key_of("DATA_PLANET_CLASS_", definition.id),
                     definition.name);
  }
  const auto &definition = stellar::core::planet_subclass_definition(
      appearance.primary_class, appearance.subclass);
  return data_name(locale, key_of("DATA_SUBCLASS_", definition.id),
                   definition.name);
}

[[nodiscard]] inline std::string species_display_name(
    const stellar::engine::LocalizationTable *locale, std::string_view species_id,
    std::string_view authored_name) {
  return data_name(locale, key_of("DATA_SPECIES_", species_id), authored_name);
}

[[nodiscard]] inline std::string phenomenon_name(
    const stellar::engine::LocalizationTable *locale,
    const stellar::core::PhenomenonDefinition &definition) {
  return data_name(locale, key_of("DATA_PHENOMENON_", definition.id),
                   definition.name);
}

[[nodiscard]] inline std::string phenomenon_description(
    const stellar::engine::LocalizationTable *locale,
    const stellar::core::PhenomenonDefinition &definition) {
  return data_name(locale, key_of("DATA_PHENOMENON_DESC_", definition.id),
                   definition.description);
}

[[nodiscard]] inline std::string construction_project_name(
    const stellar::engine::LocalizationTable *locale,
    const stellar::core::ConstructionProjectDefinition &definition) {
  return data_name(locale, key_of("DATA_PROJECT_NAME_", definition.id),
                   definition.name);
}

[[nodiscard]] inline std::string construction_project_description(
    const stellar::engine::LocalizationTable *locale,
    const stellar::core::ConstructionProjectDefinition &definition) {
  return data_name(locale, key_of("DATA_PROJECT_DESC_", definition.id),
                   definition.description);
}

[[nodiscard]] inline std::string ship_design_name(
    const stellar::engine::LocalizationTable *locale,
    const stellar::core::ShipDesignDefinition &definition) {
  return data_name(locale, key_of("DATA_DESIGN_", definition.id),
                   definition.name);
}

[[nodiscard]] inline std::string ship_design_description(
    const stellar::engine::LocalizationTable *locale,
    const stellar::core::ShipDesignDefinition &definition) {
  return data_name(locale, key_of("DATA_DESIGN_DESC_", definition.id),
                   definition.description);
}

[[nodiscard]] inline std::string surface_building_name(
    const stellar::engine::LocalizationTable *locale,
    const stellar::core::SurfaceBuildingDefinition &definition) {
  return data_name(locale, key_of("DATA_BUILDING_", definition.id),
                   definition.name);
}

[[nodiscard]] inline std::string surface_building_description(
    const stellar::engine::LocalizationTable *locale,
    const stellar::core::SurfaceBuildingDefinition &definition) {
  return data_name(locale, key_of("DATA_BUILDING_DESC_", definition.id),
                   definition.description);
}

[[nodiscard]] inline std::string shipbuilding_capability_name(
    const stellar::engine::LocalizationTable *locale,
    std::string_view capability_id, std::string_view authored_name) {
  return data_name(locale, key_of("DATA_CAPABILITY_", capability_id),
                   authored_name);
}

// Core composes result strings that embed authored English display names
// (requirement lists, lock reasons, per-building outcomes). Substitute the
// localized name for each known authored name so localized skeletons never
// carry English fragments; unrecognized text passes through unchanged.
[[nodiscard]] inline std::string localized_authored_fragment(
    const stellar::engine::LocalizationTable *locale, std::string_view text) {
  static const std::pair<std::string_view, std::string_view> authored[] = {
      {"Pathfinder Scout", "DATA_DESIGN_WARP_SCOUT"},
      {"Deep-Space Science Vessel", "DATA_DESIGN_SCIENCE_VESSEL"},
      {"Patrol Corvette", "DATA_DESIGN_PATROL_CORVETTE"},
      {"Interstellar Colony Ship", "DATA_DESIGN_COLONY_SHIP"},
      {"Sealed Resource Outpost Vessel", "DATA_DESIGN_RESOURCE_OUTPOST_SHIP"},
      {"Interstellar Bulk Freighter", "DATA_DESIGN_BULK_FREIGHTER"},
      {"Planetary Research Network", "DATA_PROJECT_NAME_RESEARCH_NETWORK"},
      {"Industrial Automation Program",
       "DATA_PROJECT_NAME_INDUSTRIAL_AUTOMATION"},
      {"Orbital Launch Complex", "DATA_PROJECT_NAME_ORBITAL_LAUNCH_COMPLEX"},
      {"Orbital Shipyard", "DATA_PROJECT_NAME_ORBITAL_SHIPYARD"},
      {"Asteroid Resource Network",
       "DATA_PROJECT_NAME_ASTEROID_RESOURCE_NETWORK"},
      {"Warp Test Facility", "DATA_PROJECT_NAME_WARP_TEST_FACILITY"},
      {"Spacecraft Construction", "DATA_CAPABILITY_SPACECRAFT_CONSTRUCTION"},
      {"Experimental Interstellar Transit",
       "DATA_CAPABILITY_EXPERIMENTAL_INTERSTELLAR_TRANSIT"},
      {"Reliable Interstellar Transit", "DATA_CAPABILITY_RELIABLE_FTL"},
      {"Extended Interstellar Transit",
       "DATA_CAPABILITY_EXTENDED_FTL_RANGE"},
      {"Practical Fusion Power", "DATA_UPGRADE_REQ_FUSION_POWER"},
      {"Advanced Additive Manufacturing",
       "DATA_UPGRADE_REQ_ADDITIVE_MANUFACTURING"},
      {"Interplanetary Trade Standards",
       "DATA_UPGRADE_REQ_INTERPLANETARY_TRADE_STANDARDS"},
      {"Closed-Loop Recycling", "DATA_UPGRADE_REQ_CLOSED_LOOP_RECYCLING"},
      {"Power generator", "DATA_BUILDING_POWER_GENERATOR"},
      {"Science lab", "DATA_BUILDING_SCIENCE_LAB"},
      {"Fabricator", "DATA_BUILDING_FABRICATOR"},
      {"Trade hub", "DATA_BUILDING_TRADE_HUB"},
      {"Habitat complex", "DATA_BUILDING_HABITAT_COMPLEX"},
      {"Controlled agriculture", "DATA_BUILDING_CONTROLLED_AGRICULTURE"},
      {"Water reclamation", "DATA_BUILDING_WATER_RECLAMATION"},
      {"Grid battery complex", "DATA_BUILDING_GRID_BATTERY"},
      {"Cargo terminal", "DATA_BUILDING_CARGO_TERMINAL"},
      {"Fusion power complex", "DATA_BUILDING_ADVANCED_POWER_GENERATOR"},
      {"Advanced science campus", "DATA_BUILDING_ADVANCED_SCIENCE_LAB"},
      {"Automated fabrication arcology",
       "DATA_BUILDING_ADVANCED_FABRICATOR"},
      {"Interstellar trade exchange", "DATA_BUILDING_ADVANCED_TRADE_HUB"},
      {"Closed-loop habitat arcology",
       "DATA_BUILDING_ADVANCED_HABITAT_COMPLEX"},
  };
  std::string out{text};
  if (!locale) return out;
  for (const auto &[name, key] : authored)
    if (locale->contains(key)) {
      const std::string localized{locale->translate(key)};
      for (std::size_t at = out.find(name); at != std::string::npos;
           at = out.find(name, at + localized.size()))
        out.replace(at, name.size(), localized);
    }
  return out;
}

// Surface hub/building upgrade lock reasons are stable core
// literals/skeletons; recompose them through the locale table. Shared by the
// colony view's lock fields and the surface-management denial path.
[[nodiscard]] inline std::string surface_lock_reason(
    const stellar::engine::LocalizationTable *locale,
    const std::string &message) {
  const auto tr = [&](std::string_view key, std::string_view fallback) {
    return locale && locale->contains(key)
               ? std::string(locale->translate(key))
               : std::string(fallback);
  };
  if (message ==
      "Complete the Industrial Automation Program before expanding this "
      "command center.") {
    if (locale && locale->contains("SURFACE_LOCK_HUB_INDUSTRY")) {
      const std::string args[] = {localized_authored_fragment(
          locale, "Industrial Automation Program")};
      return locale->format("SURFACE_LOCK_HUB_INDUSTRY", args);
    }
    return localized_authored_fragment(locale, message);
  }
  if (message ==
      "Establish Orbital Manufacturing before expanding to a level-3 "
      "planetary hub.")
    return tr("SURFACE_LOCK_HUB_ORBITAL", message);
  constexpr std::string_view small_suffix =
      " is too small for a 64-module regional hub. Keep this settlement at "
      "level 2 or expand through orbital infrastructure.";
  if (message.size() > small_suffix.size() &&
      message.ends_with(small_suffix)) {
    if (locale && locale->contains("SURFACE_LOCK_HUB_SMALL")) {
      const std::string args[] = {
          message.substr(0, message.size() - small_suffix.size())};
      return locale->format("SURFACE_LOCK_HUB_SMALL", args);
    }
    return message;
  }
  if (message.starts_with("Research ") &&
      message.ends_with(" before authorizing this upgrade.")) {
    const auto req = std::string_view(message).substr(
        9, message.size() - 9 - 33);
    const std::string name = localized_authored_fragment(locale, req);
    if (locale && locale->contains("SURFACE_LOCK_RESEARCH")) {
      const std::string args[] = {name};
      return locale->format("SURFACE_LOCK_RESEARCH", args);
    }
    return "Research " + name + " before authorizing this upgrade.";
  }
  return message;
}

} // namespace stellar::native_data
