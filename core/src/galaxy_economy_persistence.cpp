#include <stellar/core/galaxy_economy_persistence.hpp>

#include <stellar/core/construction_projects.hpp>
#include <stellar/core/species_environment.hpp>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <stdexcept>
#include <string_view>
#include <unordered_set>

namespace stellar::core {
namespace {

bool consume_dotnet_whitespace(std::string_view &value) noexcept {
  if (value.empty()) return false;
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
  if (value.size() < length) return false;
  for (std::size_t index = 1; index < length; ++index) {
    const auto continuation = static_cast<unsigned char>(value[index]);
    if ((continuation & 0xc0) != 0x80) return false;
    code_point = (code_point << 6) | (continuation & 0x3f);
  }
  value.remove_prefix(length);
  return (code_point >= 0x09 && code_point <= 0x0d) || code_point == 0x20 ||
         code_point == 0x85 || code_point == 0xa0 || code_point == 0x1680 ||
         (code_point >= 0x2000 && code_point <= 0x200a) ||
         code_point == 0x2028 || code_point == 0x2029 ||
         code_point == 0x202f || code_point == 0x205f ||
         code_point == 0x3000;
}

bool blank(std::string_view value) noexcept {
  if (value.empty()) return true;
  while (!value.empty())
    if (!consume_dotnet_whitespace(value)) return false;
  return true;
}

bool known_species(std::string_view id) {
  const auto profiles = species_environment_profiles();
  return std::any_of(profiles.begin(), profiles.end(), [&](const auto &profile) {
    return profile.id == id;
  });
}

std::string require_population_species(const std::optional<std::string> &id,
                                       const std::string &owner) {
  if (!id || blank(*id) || !known_species(*id))
    throw GalaxyEconomyPersistenceDataError(owner +
                             " references unknown population species ID '" +
                             (id ? *id : std::string{}) + "'.");
  return *id;
}

std::string resolve_population_species(
    const std::optional<std::string> &saved, int civilization_id,
    std::span<const Civilization> civilizations, int version,
    const std::string &owner) {
  if (version >= 8) return require_population_species(saved, owner);
  const auto civilization = std::find_if(
      civilizations.begin(), civilizations.end(), [&](const auto &value) {
        return value.id == civilization_id;
      });
  if (civilization == civilizations.end())
    throw GalaxyEconomyPersistenceDataError("Population state references unknown civilization " +
                             std::to_string(civilization_id) + ".");
  return require_population_species(civilization->species_id,
                                    "civilization " +
                                        std::to_string(civilization_id));
}

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
    }
    for (std::size_t index = 1; index < length && index < value.size(); ++index)
      code_point = (code_point << 6) |
                   (static_cast<unsigned char>(value[index]) & 0x3f);
    value.remove_prefix(std::min(length, value.size()));
    if (code_point <= 0xffff) {
      result.push_back(static_cast<std::uint16_t>(code_point));
    } else {
      code_point -= 0x10000;
      result.push_back(static_cast<std::uint16_t>(0xd800 + (code_point >> 10)));
      result.push_back(static_cast<std::uint16_t>(0xdc00 + (code_point & 0x3ff)));
    }
  }
  return result;
}

bool utf16_ordinal_less(const std::string &left, const std::string &right) {
  return utf16_units(left) < utf16_units(right);
}

bool valid_industry_priority(IndustryPriority value) noexcept {
  return value == IndustryPriority::Balanced ||
         value == IndustryPriority::InfrastructureFirst ||
         value == IndustryPriority::ShipbuildingFirst;
}

void validate_construction(const ConstructionSaveDto &dto) {
  const auto prefix = "Construction state " +
                      std::to_string(dto.civilization_id);
  if (!dto.completed_project_ids || !dto.queued_projects)
    throw GalaxyEconomyPersistenceDataError(prefix + " is missing required collections.");
  if (!std::isfinite(dto.active_project_progress) ||
      dto.active_project_progress < 0 ||
      !std::isfinite(dto.active_project_authorization_credits) ||
      dto.active_project_authorization_credits < 0)
    throw GalaxyEconomyPersistenceDataError(prefix +
                             " has invalid active progress or authorization.");
  const auto *active = dto.active_project_id
                           ? find_construction_project(*dto.active_project_id)
                           : nullptr;
  if (dto.active_project_id && !active)
    throw GalaxyEconomyPersistenceDataError(prefix +
                             " references an unknown active project.");
  if (!dto.active_project_id &&
      (dto.active_project_progress != 0 ||
       dto.active_project_authorization_credits != 0))
    throw GalaxyEconomyPersistenceDataError(prefix + " has active state without a project.");
  if (active && dto.active_project_progress > active->industry_cost + .0001)
    throw GalaxyEconomyPersistenceDataError(prefix + " exceeds active project materials.");

  std::unordered_set<std::string> completed;
  for (const auto &id : *dto.completed_project_ids) {
    if (!find_construction_project(id) || !completed.insert(id).second)
      throw GalaxyEconomyPersistenceDataError(prefix + " has invalid completed projects.");
  }
  if (dto.active_project_id && completed.contains(*dto.active_project_id))
    throw GalaxyEconomyPersistenceDataError(prefix +
                             " overlaps active and completed projects.");

  bool invalid_queue =
      dto.queued_projects->size() > maximum_queued_construction_projects;
  std::unordered_set<std::string> queued;
  for (const auto &order : *dto.queued_projects) {
    if (!order || blank(order->project_id) ||
        !find_construction_project(order->project_id) ||
        !std::isfinite(order->authorization_credits) ||
        order->authorization_credits < 0 ||
        !queued.insert(order->project_id).second ||
        completed.contains(order->project_id) ||
        (dto.active_project_id && order->project_id == *dto.active_project_id))
      invalid_queue = true;
  }
  if (invalid_queue)
    throw GalaxyEconomyPersistenceDataError(prefix + " has an invalid queued project.");
}

} // namespace

std::vector<Colony> restore_colony_dtos(
    std::span<const ColonySaveDto> dtos,
    std::span<const Civilization> civilizations, int save_format_version) {
  std::vector<Colony> result;
  result.reserve(dtos.size());
  for (const auto &dto : dtos) {
    const auto species = resolve_population_species(
        dto.population_species_id, dto.civilization_id, civilizations,
        save_format_version, "colony " + std::to_string(dto.id));
    if (save_format_version < 12 && dto.surface_buildings &&
        !dto.surface_buildings->empty())
      throw GalaxyEconomyPersistenceDataError("Colony " + std::to_string(dto.id) +
                               " surface construction requires save format 12.");
    if (save_format_version >= 12 && !dto.surface_buildings)
      throw GalaxyEconomyPersistenceDataError("Colony " + std::to_string(dto.id) +
                               " is missing its surface construction collection.");
    result.push_back({dto.id,
                      dto.civilization_id,
                      dto.system_id,
                      save_format_version >= 8 ? dto.planetary_body_id
                                               : std::nullopt,
                      dto.name,
                      dto.kind,
                      species,
                      dto.population_millions,
                      dto.infrastructure,
                      dto.stability,
                      dto.stored_food_population_days_millions,
                      dto.stored_water_population_days_millions,
                      dto.stored_extracted_materials,
                      dto.remaining_extractable_materials,
                      dto.surface_hub_level.value_or(3),
                      dto.surface_hub_upgrade_days_remaining,
                      dto.surface_buildings.value_or(
                          std::vector<SurfaceBuilding>{})});
  }
  return result;
}

std::vector<ColonySaveDto> capture_colony_dtos(
    std::span<const Colony> colonies) {
  std::vector<ColonySaveDto> result;
  result.reserve(colonies.size());
  for (const auto &colony : colonies) {
    const auto species = require_population_species(
        colony.population_species_id, "colony " + std::to_string(colony.id));
    result.push_back({colony.id,
                      colony.civilization_id,
                      colony.system_id,
                      colony.planetary_body_id,
                      colony.name,
                      colony.kind,
                      species,
                      colony.population_millions,
                      colony.infrastructure,
                      colony.stability,
                      colony.stored_food_population_days_millions,
                      colony.stored_water_population_days_millions,
                      colony.stored_extracted_materials,
                      colony.remaining_extractable_materials,
                      colony.surface_hub_level,
                      colony.surface_hub_upgrade_days_remaining,
                      colony.surface_buildings});
  }
  return result;
}

std::vector<CivilizationEconomy>
restore_economy_dtos(std::span<const EconomySaveDto> dtos) {
  std::vector<CivilizationEconomy> result;
  result.reserve(dtos.size());
  for (const auto &dto : dtos) {
    if (!std::isfinite(dto.last_research_spending_per_day) ||
        dto.last_research_spending_per_day < 0 ||
        !std::isfinite(dto.last_research_funding_fraction) ||
        dto.last_research_funding_fraction < 0 ||
        dto.last_research_funding_fraction > 1 ||
        !std::isfinite(dto.operating_arrears) || dto.operating_arrears < 0 ||
        !std::isfinite(dto.last_base_operations_funding_fraction) ||
        dto.last_base_operations_funding_fraction < 0 ||
        dto.last_base_operations_funding_fraction > 1)
      throw GalaxyEconomyPersistenceDataError(
          "Civilization " + std::to_string(dto.civilization_id) +
          " has invalid research funding state.");
    if (dto.industry_priority &&
        !valid_industry_priority(*dto.industry_priority))
      throw GalaxyEconomyPersistenceDataError(
          "Civilization " + std::to_string(dto.civilization_id) +
          " has an unknown industry priority.");
    result.push_back({dto.civilization_id,
                      dto.credits,
                      dto.industry,
                      dto.science,
                      dto.last_credits_per_second,
                      dto.last_industry_per_second,
                      dto.last_science_per_second,
                      dto.last_research_spending_per_day,
                      dto.last_research_funding_fraction,
                      dto.operating_arrears,
                      dto.last_base_operations_funding_fraction,
                      dto.industry_priority});
  }
  return result;
}

std::vector<EconomySaveDto> capture_economy_dtos(
    std::span<const CivilizationEconomy> economies) {
  std::vector<EconomySaveDto> result;
  result.reserve(economies.size());
  for (const auto &economy : economies) {
    if (economy.industry_priority &&
        !valid_industry_priority(*economy.industry_priority))
      throw GalaxyEconomyPersistenceDataError(
          "Civilization " + std::to_string(economy.civilization_id) +
          " has an unknown industry priority.");
    result.push_back({economy.civilization_id,
                      economy.credits,
                      economy.industry,
                      economy.science,
                      economy.last_credits_per_second,
                      economy.last_industry_per_second,
                      economy.last_science_per_second,
                      economy.last_research_spending_per_day,
                      economy.last_research_funding_fraction,
                      economy.operating_arrears,
                      economy.last_base_operations_funding_fraction,
                      economy.industry_priority});
  }
  return result;
}

std::vector<TechnologyState>
restore_technology_dtos(std::span<const TechnologySaveDto> dtos) {
  std::vector<TechnologyState> result;
  result.reserve(dtos.size());
  for (const auto &dto : dtos) {
    if (!dto.completed_technology_ids)
      throw GalaxyEconomyPersistenceNullReferenceError(
          "Object reference not set to an instance of an object.");
    TechnologyState state;
    state.civilization_id = dto.civilization_id;
    state.active_research_id = dto.active_research_id;
    state.active_research_progress = dto.active_research_progress;
    for (const auto &id : *dto.completed_technology_ids)
      state.completed_technology_ids.insert(id);
    result.push_back(std::move(state));
  }
  return result;
}

std::vector<TechnologySaveDto> capture_technology_dtos(
    std::span<const TechnologyState> technologies) {
  std::vector<TechnologySaveDto> result;
  result.reserve(technologies.size());
  for (const auto &technology : technologies) {
    std::vector<std::string> completed(
        technology.completed_technology_ids.values().begin(),
        technology.completed_technology_ids.values().end());
    std::sort(completed.begin(), completed.end(), utf16_ordinal_less);
    result.push_back({technology.civilization_id, std::move(completed),
                      technology.active_research_id,
                      technology.active_research_progress});
  }
  return result;
}

std::vector<ConstructionState>
restore_construction_dtos(std::span<const ConstructionSaveDto> dtos) {
  std::vector<ConstructionState> result;
  result.reserve(dtos.size());
  for (const auto &dto : dtos) {
    validate_construction(dto);
    ConstructionState state;
    state.civilization_id = dto.civilization_id;
    state.active_project_id = dto.active_project_id;
    state.active_project_progress = dto.active_project_progress;
    state.active_project_authorization_credits =
        dto.active_project_authorization_credits;
    state.completed_project_ids = *dto.completed_project_ids;
    for (const auto &order : *dto.queued_projects)
      state.queued_projects.push_back(
          {order->project_id, order->authorization_credits});
    result.push_back(std::move(state));
  }
  return result;
}

std::vector<ConstructionSaveDto> capture_construction_dtos(
    std::span<const ConstructionState> states) {
  std::vector<ConstructionSaveDto> result;
  result.reserve(states.size());
  for (const auto &state : states) {
    auto completed = state.completed_project_ids;
    std::sort(completed.begin(), completed.end(), utf16_ordinal_less);
    std::vector<std::optional<QueuedConstructionProjectSaveDto>> queued;
    queued.reserve(state.queued_projects.size());
    for (const auto &order : state.queued_projects)
      queued.push_back(QueuedConstructionProjectSaveDto{
          order.project_id, order.authorization_credits});
    result.push_back({state.civilization_id,
                      std::move(completed),
                      state.active_project_id,
                      state.active_project_progress,
                      state.active_project_authorization_credits,
                      std::move(queued)});
  }
  return result;
}

} // namespace stellar::core
