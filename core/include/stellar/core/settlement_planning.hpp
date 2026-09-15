#pragma once

#include <stellar/core/colony_operations.hpp>
#include <stellar/core/fleet_reach.hpp>
#include <stellar/core/settlement_knowledge.hpp>
#include <stellar/core/sovereign_currency.hpp>

#include <functional>

namespace stellar::core {

struct SettlementPlanningWorldView {
  std::span<const StellarSystem> systems;
  std::span<const PlanetaryBody> bodies;
  std::span<const Civilization> civilizations;
  std::span<const Colony> colonies;
  std::span<const FleetState> fleets;
  std::span<const CivilizationEconomy> economies;
  const CivilizationKnowledgeState &knowledge;
  InterstellarLaneNetwork &lanes;
  SettlementKnowledgeWorldView knowledge_view() const {
    return {systems, bodies, civilizations, colonies, fleets, knowledge};
  }
};

using SettlementReachAssessment = std::function<MissionReachAssessment(
    OperationalReachWorldView, int, const FleetState &, int,
    InterstellarMissionKind)>;

struct ColonizationOpportunityCandidate {
  int system_id{};
  std::string system_name;
  int planetary_body_id{};
  std::string planetary_body_name;
  PlanetaryBodyKind planetary_body_kind{};
  int fleet_id{};
  std::string passenger_species_id;
  SpeciesColonizationViability colonization_viability{};
  double natural_habitability{}, unprotected_operational_capacity{};
  EnvironmentalLimitingFactor limiting_factor{};
  bool requires_gravity_mitigation{}, requires_thermal_control{},
      requires_pressure_control{}, requires_sealed_habitat{},
      requires_artificial_biosphere{}, requires_radiation_shielding{};
  bool has_solid_surface{}, has_native_pre_warp_civilization{},
      has_rare_resource{}, system_occupied{};
  double distance_from_fleet{};
  MissionReachAssessment reach;
  bool can_order{};
  std::string reason;
  bool system_reserved_by_friendly_colony_mission{};
  std::optional<int> reserved_by_fleet_id;
};
struct ColonizationOpportunityPlan {
  int fleet_id{};
  std::string fleet_name;
  int civilization_id{-1};
  std::string passenger_species_id, passenger_species_name;
  double embarked_population_millions{};
  bool can_receive_orders{};
  std::string status;
  std::vector<ColonizationOpportunityCandidate> candidates;
};
struct ColonizationOrderAssessment {
  bool accepted{};
  std::string message;
  std::optional<ColonizationOpportunityCandidate> candidate;
};

struct ResourceOutpostOpportunityCandidate {
  int system_id{};
  std::string system_name;
  int planetary_body_id{};
  std::string planetary_body_name;
  int fleet_id{};
  std::string personnel_species_id;
  double natural_habitability{}, unprotected_operational_capacity{};
  EnvironmentalLimitingFactor limiting_factor{};
  bool has_rare_resource{};
  std::string deposit_material_name, deposit_grade;
  double deposit_accessibility{}, extraction_yield_multiplier{},
      initial_deposit_materials{};
  bool system_occupied{}, is_too_harsh_for_colony{};
  double distance_from_fleet{};
  MissionReachAssessment reach;
  bool can_order{};
  std::string reason;
};
struct ResourceOutpostOpportunityPlan {
  int fleet_id{};
  std::string fleet_name;
  int civilization_id{-1};
  std::string personnel_species_id, personnel_species_name;
  double personnel_millions{};
  bool can_receive_orders{};
  std::string status;
  std::vector<ResourceOutpostOpportunityCandidate> candidates;
};
struct ResourceOutpostOrderAssessment {
  bool accepted{};
  std::string message;
  std::optional<ResourceOutpostOpportunityCandidate> candidate;
};

class ColonizationOpportunityPlanner {
public:
  static constexpr int default_maximum_candidates = 32,
                       hard_maximum_candidates = 64;
  explicit ColonizationOpportunityPlanner(SettlementReachAssessment reach = {});
  ColonizationOpportunityPlan
  build_plan(SettlementPlanningWorldView, int,
             int maximum_candidates = default_maximum_candidates) const;
  ColonizationOrderAssessment assess_order(SettlementPlanningWorldView, int,
                                           int, int) const;
  MissionReachAssessment assess_operational_reach(SettlementPlanningWorldView,
                                                  int, int) const;

private:
  SettlementReachAssessment reach_;
};
class ResourceOutpostOpportunityPlanner {
public:
  static constexpr int default_maximum_candidates = 32,
                       hard_maximum_candidates = 64;
  explicit ResourceOutpostOpportunityPlanner(
      SettlementReachAssessment reach = {});
  ResourceOutpostOpportunityPlan
  build_plan(SettlementPlanningWorldView, int,
             int maximum_candidates = default_maximum_candidates) const;
  ResourceOutpostOrderAssessment assess_order(SettlementPlanningWorldView, int,
                                              int, int) const;
  static bool is_outpost_fleet(const FleetState &);

private:
  SettlementReachAssessment reach_;
};
} // namespace stellar::core
