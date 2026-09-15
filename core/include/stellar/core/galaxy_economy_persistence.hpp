#pragma once

#include <stellar/core/colony_economy.hpp>
#include <stellar/core/construction_state.hpp>
#include <stellar/core/legacy_technology.hpp>

#include <optional>
#include <span>
#include <stdexcept>
#include <string>
#include <vector>

namespace stellar::core {

class GalaxyEconomyPersistenceDataError : public std::runtime_error {
public:
  using std::runtime_error::runtime_error;
};

class GalaxyEconomyPersistenceNullReferenceError
    : public std::runtime_error {
public:
  using std::runtime_error::runtime_error;
};

struct ColonySaveDto {
  int id{}, civilization_id{}, system_id{};
  std::optional<int> planetary_body_id;
  std::string name;
  SettlementKind kind{SettlementKind::Colony};
  std::optional<std::string> population_species_id;
  double population_millions{}, infrastructure{}, stability{};
  double stored_food_population_days_millions{};
  double stored_water_population_days_millions{};
  double stored_extracted_materials{};
  std::optional<double> remaining_extractable_materials;
  std::optional<int> surface_hub_level;
  double surface_hub_upgrade_days_remaining{};
  std::optional<std::vector<SurfaceBuilding>> surface_buildings;
};

struct EconomySaveDto {
  int civilization_id{};
  double credits{}, industry{}, science{};
  double last_credits_per_second{}, last_industry_per_second{};
  double last_science_per_second{}, last_research_spending_per_day{};
  double last_research_funding_fraction{1.0}, operating_arrears{};
  double last_base_operations_funding_fraction{1.0};
  std::optional<IndustryPriority> industry_priority;
};

struct TechnologySaveDto {
  int civilization_id{};
  std::optional<std::vector<std::string>> completed_technology_ids;
  std::optional<std::string> active_research_id;
  double active_research_progress{};
};

struct QueuedConstructionProjectSaveDto {
  std::string project_id;
  double authorization_credits{};
};

struct ConstructionSaveDto {
  int civilization_id{};
  std::optional<std::vector<std::string>> completed_project_ids;
  std::optional<std::string> active_project_id;
  double active_project_progress{}, active_project_authorization_credits{};
  std::optional<std::vector<std::optional<QueuedConstructionProjectSaveDto>>>
      queued_projects;
};

std::vector<Colony> restore_colony_dtos(
    std::span<const ColonySaveDto> dtos,
    std::span<const Civilization> civilizations, int save_format_version);
std::vector<ColonySaveDto> capture_colony_dtos(std::span<const Colony> colonies);

std::vector<CivilizationEconomy>
restore_economy_dtos(std::span<const EconomySaveDto> dtos);
std::vector<EconomySaveDto>
capture_economy_dtos(std::span<const CivilizationEconomy> economies);

std::vector<TechnologyState>
restore_technology_dtos(std::span<const TechnologySaveDto> dtos);
std::vector<TechnologySaveDto>
capture_technology_dtos(std::span<const TechnologyState> technologies);

std::vector<ConstructionState>
restore_construction_dtos(std::span<const ConstructionSaveDto> dtos);
std::vector<ConstructionSaveDto>
capture_construction_dtos(std::span<const ConstructionState> states);

} // namespace stellar::core
