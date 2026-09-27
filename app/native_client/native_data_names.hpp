#pragma once

#include <stellar/core/construction_projects.hpp>
#include <stellar/core/galaxy_phenomena.hpp>
#include <stellar/core/planet_appearance.hpp>
#include <stellar/core/species_environment.hpp>
#include <stellar/core/stellar_object.hpp>
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

} // namespace stellar::native_data
