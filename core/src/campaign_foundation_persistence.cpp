#include <stellar/core/campaign_foundation_persistence.hpp>

#include <stellar/core/detail/civilization_founding_roster.hpp>

#include <stellar/core/species_environment.hpp>

#include <algorithm>
#include <bit>
#include <cmath>
#include <cstdint>
#include <limits>
#include <ranges>
#include <string_view>
#include <unordered_set>
#include <utility>

namespace stellar::core {
namespace {

std::vector<std::uint16_t> utf16_units(std::string_view value) {
  std::vector<std::uint16_t> result;
  while (!value.empty()) {
    const auto first = static_cast<unsigned char>(value.front());
    std::uint32_t code_point = first;
    std::size_t length = 1;
    if ((first & 0xe0) == 0xc0) {
      code_point = first & 0x1f;
      length = 2;
    } else if ((first & 0xf0) == 0xe0) {
      code_point = first & 0x0f;
      length = 3;
    } else if ((first & 0xf8) == 0xf0) {
      code_point = first & 0x07;
      length = 4;
    } else if (first >= 0x80) {
      throw CampaignFoundationPersistenceArgumentError(
          "An office assignment contains invalid character metadata.");
    }
    if (value.size() < length)
      throw CampaignFoundationPersistenceArgumentError(
          "An office assignment contains invalid character metadata.");
    for (std::size_t index = 1; index < length; ++index) {
      const auto continuation = static_cast<unsigned char>(value[index]);
      if ((continuation & 0xc0) != 0x80)
        throw CampaignFoundationPersistenceArgumentError(
            "An office assignment contains invalid character metadata.");
      code_point = (code_point << 6) | (continuation & 0x3f);
    }
    value.remove_prefix(length);
    if (code_point <= 0xffff) {
      result.push_back(static_cast<std::uint16_t>(code_point));
    } else {
      code_point -= 0x10000;
      result.push_back(static_cast<std::uint16_t>(0xd800 + (code_point >> 10)));
      result.push_back(
          static_cast<std::uint16_t>(0xdc00 + (code_point & 0x3ff)));
    }
  }
  return result;
}

bool ordinal_less(std::string_view left, std::string_view right) {
  return utf16_units(left) < utf16_units(right);
}

bool whitespace(std::uint32_t value) {
  return (value >= 0x09 && value <= 0x0d) || value == 0x20 || value == 0x85 ||
         value == 0xa0 || value == 0x1680 ||
         (value >= 0x2000 && value <= 0x200a) || value == 0x2028 ||
         value == 0x2029 || value == 0x202f || value == 0x205f ||
         value == 0x3000;
}

bool blank(std::string_view value) {
  if (value.empty())
    return true;
  while (!value.empty()) {
    const auto first = static_cast<unsigned char>(value.front());
    std::uint32_t code_point = first;
    std::size_t length = 1;
    if ((first & 0xe0) == 0xc0) {
      code_point = first & 0x1f;
      length = 2;
    } else if ((first & 0xf0) == 0xe0) {
      code_point = first & 0x0f;
      length = 3;
    } else if ((first & 0xf8) == 0xf0) {
      code_point = first & 0x07;
      length = 4;
    } else if (first >= 0x80) {
      return false;
    }
    if (value.size() < length)
      return false;
    for (std::size_t index = 1; index < length; ++index) {
      const auto continuation = static_cast<unsigned char>(value[index]);
      if ((continuation & 0xc0) != 0x80)
        return false;
      code_point = (code_point << 6) | (continuation & 0x3f);
    }
    if (!whitespace(code_point))
      return false;
    value.remove_prefix(length);
  }
  return true;
}

std::size_t utf16_length(std::string_view value) {
  return utf16_units(value).size();
}

bool known_species(std::string_view id) noexcept {
  return std::ranges::any_of(
      species_environment_profiles(),
      [id](const auto &profile) { return profile.id == id; });
}

std::string require_species(std::string_view id, int civilization_id) {
  if (blank(id) || !known_species(id))
    throw CampaignFoundationPersistenceDataError(
        "Civilization " + std::to_string(civilization_id) +
        " references unknown species ID '" + std::string(id) + "'.");
  return std::string(id);
}

void validate_character(std::string_view office,
                        const CivilizationCharacter &character) {
  if (blank(office) || utf16_length(office) > 64 || blank(character.id) ||
      utf16_length(character.id) > 128 || blank(character.display_name) ||
      utf16_length(character.display_name) > 160 ||
      (character.voice_profile_id &&
       utf16_length(*character.voice_profile_id) > 128) ||
      (character.portrait && utf16_length(*character.portrait) > 512))
    throw CampaignFoundationPersistenceArgumentError(
        "An office assignment contains invalid character metadata.");
}

std::vector<CivilizationOffice>
restore_leadership(const CivilizationPersistenceDto &source,
                   std::string_view species) {
  if (!source.leadership_present)
    return detail::civilization_founding_roster(source.id,
                                                species == "terran_baseline");
  auto entries = source.leadership;
  std::stable_sort(entries.begin(), entries.end(),
                   [](const auto &left, const auto &right) {
                     return ordinal_less(left.office, right.office);
                   });
  std::vector<CivilizationOffice> result;
  result.reserve(entries.size());
  for (const auto &entry : entries) {
    if (!entry.character)
      throw CampaignFoundationPersistenceNullArgumentError(
          "Value cannot be null. (Parameter 'character')");
    validate_character(entry.office, *entry.character);
    const auto found =
        std::ranges::find(result, entry.office, &CivilizationOffice::office);
    if (found != result.end()) {
      found->character = *entry.character;
      continue;
    }
    if (result.size() >= 32)
      throw CampaignFoundationPersistenceOperationError(
          "A civilization cannot hold more than 32 named offices.");
    result.push_back({entry.office, *entry.character});
  }
  return result;
}

void validate_stellar_catalog(std::span<const StellarSystem> systems) {
  for (const auto &system : systems) {
    if (system.position.depth_light_years &&
        !std::isfinite(*system.position.depth_light_years))
      throw CampaignFoundationPersistenceDataError(
          "System " + std::to_string(system.id) + " (" + system.name +
          ") has an invalid galactic depth.");
    const auto valid_class = [](const std::optional<StellarClass> value) {
      return !value || (static_cast<int>(*value) >= 0 &&
                        static_cast<int>(*value) <=
                            static_cast<int>(StellarClass::Pulsar));
    };
    if (!valid_class(system.primary) || !valid_class(system.secondary) ||
        !valid_class(system.tertiary))
      throw CampaignFoundationPersistenceDataError(
          "System " + std::to_string(system.id) + " (" + system.name +
          ") has an invalid stellar class.");
    if ((system.secondary && !system.primary) ||
        (system.tertiary && !system.secondary))
      throw CampaignFoundationPersistenceDataError(
          "System " + std::to_string(system.id) + " (" + system.name +
          ") has an incomplete stellar companion configuration; B requires A "
          "and C requires B.");
    if (system.catalog_preset_id &&
        *system.catalog_preset_id == sol_catalog_preset_id &&
        (system.secondary || system.tertiary))
      throw CampaignFoundationPersistenceDataError(
          "System " + std::to_string(system.id) +
          " (Sol) must retain its single canonical star.");
  }
}

} // namespace

CampaignFoundationPersistenceDataError::CampaignFoundationPersistenceDataError(
    std::string message)
    : std::runtime_error(std::move(message)) {}
CampaignFoundationPersistenceArgumentError::
    CampaignFoundationPersistenceArgumentError(std::string message)
    : std::runtime_error(std::move(message)) {}
CampaignFoundationPersistenceNullArgumentError::
    CampaignFoundationPersistenceNullArgumentError(std::string message)
    : std::runtime_error(std::move(message)) {}
CampaignFoundationPersistenceRangeError::
    CampaignFoundationPersistenceRangeError(std::string message)
    : std::runtime_error(std::move(message)) {}
CampaignFoundationPersistenceOperationError::
    CampaignFoundationPersistenceOperationError(std::string message)
    : std::runtime_error(std::move(message)) {}

std::vector<StellarSystem>
restore_stellar_systems(std::span<const StellarSystemPersistenceDto> source) {
  std::vector<StellarSystem> result;
  result.reserve(source.size());
  for (const auto &value : source)
    result.push_back({value.id,
                      value.name,
                      {value.x, value.y, value.galactic_depth_light_years},
                      value.stellar_class,
                      value.secondary_stellar_class,
                      value.tertiary_stellar_class,
                      value.catalog_preset_id,
                      value.stellar_catalog_id,
                      value.archetype,
                      value.has_habitable_world,
                      value.has_anomaly,
                      value.has_rare_resource,
                      value.has_pre_warp_civilization, value.stellar_object, value.engulfed_planets, value.stellar_region, value.small_body_fields, value.stellar_orbits, value.stellar_activity});
  validate_stellar_catalog(result);
  for (const auto& s:result) { validate_stellar_orbits(s); validate_stellar_activity(s); if(s.stellar_object) validate_stellar_physics(*s.stellar_object); if(s.engulfed_planets<0) throw std::invalid_argument("Invalid engulfed count"); if(s.stellar_region&&static_cast<unsigned>(*s.stellar_region)>=12)throw std::invalid_argument("Invalid stellar region"); }
  return result;
}

std::vector<StellarSystemPersistenceDto>
capture_stellar_systems(std::span<const StellarSystem> source) {
  validate_stellar_catalog(source);
  for (const auto& s:source) if(s.stellar_object) validate_stellar_physics(*s.stellar_object);
  std::vector<StellarSystemPersistenceDto> result;
  result.reserve(source.size());
  for (const auto &value : source)
    result.push_back({value.id, value.name, value.position.x, value.position.y,
                      value.archetype, value.has_habitable_world,
                      value.has_anomaly, value.has_rare_resource,
                      value.has_pre_warp_civilization, value.catalog_preset_id,
                      value.primary, value.secondary, value.tertiary,
                      value.position.depth_light_years,
                      value.stellar_catalog_id, value.stellar_object, value.engulfed_planets, value.stellar_region, value.small_body_fields, value.stellar_orbits, value.stellar_activity});
  return result;
}

std::vector<Civilization>
restore_civilizations(std::span<const CivilizationPersistenceDto> source,
                      bool legacy_already_warp_capable, int save_format_version,
                      std::int64_t campaign_seed) {
  std::vector<Civilization> result;
  result.reserve(source.size());
  for (const auto &value : source) {
    std::string species;
    if (save_format_version < 8) {
      if (value.id < 0)
        throw CampaignFoundationPersistenceRangeError(
            "Specified argument was out of the range of valid values. "
            "(Parameter 'civilizationId')");
      species = assign_species(campaign_seed, value.id, false);
    } else {
      species = require_species(value.species_id, value.id);
    }
    Civilization civilization{
        value.id,
        value.name,
        value.home_system_id,
        value.archetype,
        value.traits,
        value.is_player,
        legacy_already_warp_capable ? CivilizationDevelopmentStage::WarpCapable
                                    : value.development_stage,
        legacy_already_warp_capable ? false : value.is_seeded_ancient,
        legacy_already_warp_capable ? true : value.expansion_allowed,
        legacy_already_warp_capable ? false : value.neutral_unless_provoked,
        species,
        {}};
    civilization.leadership = restore_leadership(value, species);
    result.push_back(std::move(civilization));
  }
  return result;
}

std::vector<CivilizationPersistenceDto>
capture_civilizations(std::span<const Civilization> source) {
  std::vector<CivilizationPersistenceDto> result;
  result.reserve(source.size());
  for (const auto &value : source) {
    CivilizationPersistenceDto dto;
    dto.leadership_present = true;
    dto.leadership.reserve(value.leadership.size());
    for (const auto &office : value.leadership)
      dto.leadership.push_back({office.office, office.character});
    dto.id = value.id;
    dto.name = value.name;
    dto.home_system_id = value.home_system_id;
    dto.archetype = value.archetype;
    dto.traits = value.traits;
    dto.is_player = value.is_player;
    dto.development_stage = value.development_stage;
    dto.is_seeded_ancient = value.is_seeded_ancient;
    dto.expansion_allowed = value.expansion_allowed;
    dto.neutral_unless_provoked = value.neutral_unless_provoked;
    dto.species_id = require_species(value.species_id, value.id);
    result.push_back(std::move(dto));
  }
  return result;
}

} // namespace stellar::core
