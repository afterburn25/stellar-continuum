#include "native_settlement_mission_controller.hpp"

#include <stellar/core/colonization_runtime.hpp>
#include <stellar/core/species_environment.hpp>

#include <algorithm>
#include <ranges>
#include <stdexcept>

namespace stellar::native_colony {
namespace {
using namespace stellar::core;

struct Context {
  IntegratedAdaptiveCampaignRuntime &runtime;
  CampaignSimulationState &simulation;
  FreshCampaignState &world;
  const Civilization &player;
  CivilizationEconomy &economy;
};

Context context(CampaignFrame &frame) {
  auto &runtime = frame.runtime();
  auto &simulation = runtime.world();
  auto &world = simulation.campaign();
  const auto player = std::ranges::find(world.civilizations,
                                         world.player_civilization_id,
                                         &Civilization::id);
  const auto economy = std::ranges::find(world.economies,
                                          world.player_civilization_id,
                                          &CivilizationEconomy::civilization_id);
  if (player == world.civilizations.end() || !player->is_player ||
      economy == world.economies.end())
    throw std::runtime_error(
        "The player campaign has no complete settlement economy state.");
  return {runtime, simulation, world, *player, *economy};
}

bool same_currency(const SovereignCurrencyDefinition &left,
                   const SovereignCurrencyDefinition &right) {
  return left.name == right.name && left.code == right.code &&
         left.symbol == right.symbol &&
         left.local_units_per_budget_unit == right.local_units_per_budget_unit;
}

NativeSettlementCandidate copy(const ColonizationOpportunityCandidate &source) {
  return {.system_id = source.system_id,
          .body_id = source.planetary_body_id,
          .system_name = source.system_name,
          .body_name = source.planetary_body_name,
          .can_order = source.can_order,
          .reason = source.reason,
          .distance_from_fleet = source.distance_from_fleet,
          .reach = source.reach,
          .viability = source.colonization_viability,
          .natural_habitability = source.natural_habitability,
          .unprotected_operational_capacity =
              source.unprotected_operational_capacity,
          .limiting_factor = source.limiting_factor,
          .requires_gravity_mitigation = source.requires_gravity_mitigation,
          .requires_thermal_control = source.requires_thermal_control,
          .requires_pressure_control = source.requires_pressure_control,
          .requires_sealed_habitat = source.requires_sealed_habitat,
          .requires_artificial_biosphere =
              source.requires_artificial_biosphere,
          .requires_radiation_shielding =
              source.requires_radiation_shielding,
          .system_reserved_by_friendly_colony_mission =
              source.system_reserved_by_friendly_colony_mission,
          .reserved_by_fleet_id = source.reserved_by_fleet_id};
}

NativeSettlementCandidate
copy(const ResourceOutpostOpportunityCandidate &source) {
  return {.system_id = source.system_id,
          .body_id = source.planetary_body_id,
          .system_name = source.system_name,
          .body_name = source.planetary_body_name,
          .can_order = source.can_order,
          .reason = source.reason,
          .distance_from_fleet = source.distance_from_fleet,
          .reach = source.reach,
          .viability = SpeciesColonizationViability::Unsuitable,
          .natural_habitability = source.natural_habitability,
          .unprotected_operational_capacity =
              source.unprotected_operational_capacity,
          .limiting_factor = source.limiting_factor,
          .has_confirmed_deposit = source.has_rare_resource,
          .too_harsh_for_colony = source.is_too_harsh_for_colony,
          .deposit_material_name = source.deposit_material_name,
          .deposit_grade = source.deposit_grade,
          .deposit_accessibility = source.deposit_accessibility,
          .extraction_yield_multiplier = source.extraction_yield_multiplier,
          .initial_deposit_materials = source.initial_deposit_materials};
}

const NativeSettlementMissionView *find_projection(
    const std::vector<NativeSettlementMissionView> &views,
    const std::uint64_t revision, const int fleet_id) {
  const auto found = std::ranges::find_if(views, [&](const auto &view) {
    return view.revision == revision && view.fleet_id == fleet_id;
  });
  return found == views.end() ? nullptr : &*found;
}

const NativeSettlementCandidate *find_candidate(
    const NativeSettlementMissionView &view, const int system_id,
    const int body_id) {
  const auto found = std::ranges::find_if(view.candidates, [&](const auto &item) {
    return item.system_id == system_id && item.body_id == body_id;
  });
  return found == view.candidates.end() ? nullptr : &*found;
}

bool same_reach(const MissionReachAssessment &left,
                const MissionReachAssessment &right) {
  return left.is_supported == right.is_supported &&
         left.is_authoritative == right.is_authoritative &&
         left.reason == right.reason &&
         left.route_system_ids == right.route_system_ids &&
         left.route_distance_light_years == right.route_distance_light_years;
}

bool same_candidate(const NativeSettlementCandidate &left,
                    const NativeSettlementCandidate &right) {
  return left.system_id == right.system_id && left.body_id == right.body_id &&
         left.system_name == right.system_name &&
         left.body_name == right.body_name &&
         left.can_order == right.can_order && left.reason == right.reason &&
         left.distance_from_fleet == right.distance_from_fleet &&
         same_reach(left.reach, right.reach) &&
         left.viability == right.viability &&
         left.natural_habitability == right.natural_habitability &&
         left.unprotected_operational_capacity ==
             right.unprotected_operational_capacity &&
         left.limiting_factor == right.limiting_factor &&
         left.requires_gravity_mitigation ==
             right.requires_gravity_mitigation &&
         left.requires_thermal_control == right.requires_thermal_control &&
         left.requires_pressure_control == right.requires_pressure_control &&
         left.requires_sealed_habitat == right.requires_sealed_habitat &&
         left.requires_artificial_biosphere ==
             right.requires_artificial_biosphere &&
         left.requires_radiation_shielding ==
             right.requires_radiation_shielding &&
         left.system_reserved_by_friendly_colony_mission ==
             right.system_reserved_by_friendly_colony_mission &&
         left.reserved_by_fleet_id == right.reserved_by_fleet_id &&
         left.has_confirmed_deposit == right.has_confirmed_deposit &&
         left.too_harsh_for_colony == right.too_harsh_for_colony &&
         left.deposit_material_name == right.deposit_material_name &&
         left.deposit_grade == right.deposit_grade &&
         left.deposit_accessibility == right.deposit_accessibility &&
         left.extraction_yield_multiplier ==
             right.extraction_yield_multiplier &&
         left.initial_deposit_materials == right.initial_deposit_materials;
}

std::string species_name(const std::string &id) {
  const auto profiles = species_environment_profiles();
  const auto found = std::ranges::find(profiles, id,
                                        &SpeciesEnvironmentProfile::id);
  return found == profiles.end() ? std::string{} : found->display_name;
}

} // namespace

void NativeSettlementMissionController::require_owner() const {
  if (std::this_thread::get_id() != owner_)
    throw std::logic_error(
        "Native settlement missions must run on the simulation owner thread.");
}

void NativeSettlementMissionController::bind_generation(
    const std::uint64_t campaign_generation) {
  if (generation_ && campaign_generation < *generation_)
    throw std::invalid_argument(
        "A stale campaign generation cannot replace settlement missions.");
  if (!generation_ || *generation_ != campaign_generation) {
    generation_ = campaign_generation;
    projected_.clear();
    exact_.reset();
    next_revision_ = 1;
  }
}

NativeSettlementTargetPreview NativeSettlementMissionController::preview_exact(
    CampaignFrame &frame, const std::uint64_t campaign_generation,
    const int fleet_id, const int destination_system_id, const int body_id) {
  require_owner();
  bind_generation(campaign_generation);
  auto current = context(frame);
  NativeSettlementTargetPreview result;
  result.campaign_generation = campaign_generation;
  result.revision = next_revision_++;
  result.player_civilization_id = current.player.id;
  result.fleet_id = fleet_id;
  result.destination_system_id = destination_system_id;
  result.body_id = body_id;

  const auto fleet = std::ranges::find(current.world.fleets, fleet_id,
                                        &FleetState::id);
  if (fleet == current.world.fleets.end() || !fleet->is_active ||
      fleet->civilization_id != current.player.id ||
      fleet->role != FleetRole::Colony ||
      fleet->embarked_population_millions <= 0.) {
    result.message =
        "No controllable populated settlement vessel with that fleet ID is available.";
    exact_ = result;
    return result;
  }

  const bool outpost =
      ResourceOutpostOpportunityPlanner::is_outpost_fleet(*fleet);
  result.kind = outpost ? NativeSettlementMissionKind::ResourceOutpost
                        : NativeSettlementMissionKind::Colony;
  result.fleet_name = fleet->name;
  result.mission_order_revision = fleet->mission_order_revision;
  result.design_id = fleet->design_id;
  result.personnel_species_id =
      fleet->embarked_population_species_id.value_or("");
  result.personnel_species_name = species_name(result.personnel_species_id);
  result.personnel_millions = fleet->embarked_population_millions;
  result.currency = sovereign_currency_for_civilization(
      current.world.civilizations, current.player.id);
  const auto authorization =
      outpost
          ? ColonizationSimulation::resource_outpost_expedition_authorization(
                *fleet)
          : ColonizationSimulation::colony_expedition_authorization(*fleet);
  result.requires_new_authorization = authorization.is_new_expedition;
  result.authorization_budget_units = authorization.charge_budget_units;
  result.treasury_budget_units = current.economy.credits;
  result.funded = !result.requires_new_authorization ||
                  current.economy.credits + .0001 >=
                      result.authorization_budget_units;
  result.formatted_authorization =
      result.currency.format(result.authorization_budget_units);
  result.formatted_treasury = result.currency.format(current.economy.credits);

  if (outpost) {
    const auto assessment =
        current.runtime.core().assess_resource_outpost_fleet_order(
            &current.simulation, current.player.id, fleet_id,
            destination_system_id, body_id);
    result.accepted = assessment.accepted;
    result.message = assessment.message;
    if (assessment.candidate) result.candidate = copy(*assessment.candidate);
  } else {
    const auto assessment = current.runtime.core().assess_colony_fleet_order(
        &current.simulation, current.player.id, fleet_id,
        destination_system_id, body_id);
    result.accepted = assessment.accepted;
    result.message = assessment.message;
    if (assessment.candidate) result.candidate = copy(*assessment.candidate);
  }
  exact_ = result;
  return result;
}

NativeSettlementCommandOutcome NativeSettlementMissionController::issue_exact(
    CampaignFrame &frame, const std::uint64_t campaign_generation,
    const std::uint64_t preview_revision) {
  require_owner();
  if (!generation_ || *generation_ != campaign_generation || !exact_ ||
      exact_->campaign_generation != campaign_generation ||
      exact_->revision != preview_revision)
    return {false,
            "That settlement destination preview is stale; preview it again.",
            0};
  const auto prior = *exact_;
  auto current = context(frame);
  if (prior.player_civilization_id != current.player.id)
    return {false,
            "The campaign changed; preview that settlement destination again.",
            0};
  const auto fleet = std::ranges::find(current.world.fleets, prior.fleet_id,
                                        &FleetState::id);
  if (fleet == current.world.fleets.end() || !fleet->is_active ||
      fleet->civilization_id != current.player.id ||
      fleet->role != FleetRole::Colony ||
      fleet->embarked_population_millions <= 0.)
    return {false, "That owned settlement vessel is no longer available.", 0};
  const bool outpost =
      ResourceOutpostOpportunityPlanner::is_outpost_fleet(*fleet);
  const auto kind = outpost ? NativeSettlementMissionKind::ResourceOutpost
                            : NativeSettlementMissionKind::Colony;
  const auto currency = sovereign_currency_for_civilization(
      current.world.civilizations, current.player.id);
  const auto authorization =
      outpost
          ? ColonizationSimulation::resource_outpost_expedition_authorization(
                *fleet)
          : ColonizationSimulation::colony_expedition_authorization(*fleet);
  if (prior.kind != kind || prior.design_id != fleet->design_id ||
      prior.mission_order_revision != fleet->mission_order_revision ||
      prior.personnel_species_id !=
          fleet->embarked_population_species_id.value_or("") ||
      prior.personnel_millions != fleet->embarked_population_millions ||
      prior.requires_new_authorization !=
          authorization.is_new_expedition ||
      prior.authorization_budget_units != authorization.charge_budget_units ||
      !same_currency(prior.currency, currency))
    return {false,
            "The settlement vessel or authorization changed; preview again.",
            fleet->mission_order_revision};

  bool accepted{};
  std::string message;
  std::optional<NativeSettlementCandidate> candidate;
  if (outpost) {
    const auto assessment =
        current.runtime.core().assess_resource_outpost_fleet_order(
            &current.simulation, current.player.id, fleet->id,
            prior.destination_system_id, prior.body_id);
    accepted = assessment.accepted;
    message = assessment.message;
    if (assessment.candidate) candidate = copy(*assessment.candidate);
  } else {
    const auto assessment = current.runtime.core().assess_colony_fleet_order(
        &current.simulation, current.player.id, fleet->id,
        prior.destination_system_id, prior.body_id);
    accepted = assessment.accepted;
    message = assessment.message;
    if (assessment.candidate) candidate = copy(*assessment.candidate);
  }
  if (!accepted) return {false, std::move(message), fleet->mission_order_revision};
  if (!prior.accepted || !prior.candidate || !candidate ||
      !same_candidate(*prior.candidate, *candidate))
    return {false,
            "That settlement destination changed; preview it again.",
            fleet->mission_order_revision};

  const auto result = outpost
                          ? current.runtime.core()
                                .issue_resource_outpost_fleet_order(
                                    &current.simulation, current.player.id,
                                    fleet->id, prior.destination_system_id,
                                    prior.body_id)
                          : current.runtime.core().issue_colony_fleet_order(
                                &current.simulation, current.player.id,
                                fleet->id, prior.destination_system_id,
                                prior.body_id);
  const auto updated = std::ranges::find(current.world.fleets, prior.fleet_id,
                                          &FleetState::id);
  if (result.accepted) {
    exact_.reset();
    projected_.clear();
  }
  return {result.accepted, result.message,
          updated == current.world.fleets.end()
              ? 0
              : updated->mission_order_revision};
}

bool NativeSettlementMissionController::is_current_generation(
    const std::uint64_t value) const noexcept {
  return generation_ && *generation_ == value;
}

std::optional<NativeSettlementLiveStatus>
NativeSettlementMissionController::live_status(
    CampaignFrame &frame, const std::uint64_t campaign_generation,
    const int fleet_id) {
  require_owner();
  bind_generation(campaign_generation);
  auto current = context(frame);
  const auto fleet = std::ranges::find(current.world.fleets, fleet_id,
                                        &FleetState::id);
  if (fleet == current.world.fleets.end() || !fleet->is_active ||
      fleet->civilization_id != current.player.id ||
      fleet->role != FleetRole::Colony ||
      fleet->embarked_population_millions <= 0.)
    return std::nullopt;
  const auto status = fleet->settlement_body_id
                          ? "Establishing settlement"
                          : fleet->destination_planetary_body_id
                                ? "Settlement mission assigned"
                                : "Ready for settlement orders";
  return NativeSettlementLiveStatus{
      fleet->id,
      status,
      fleet->destination_system_id,
      fleet->destination_planetary_body_id,
      fleet->settlement_body_id,
      fleet->settlement_days_completed,
      ColonizationSimulation::establishment_days(*fleet)};
}

std::vector<NativeSettlementMissionView>
NativeSettlementMissionController::build(
    CampaignFrame &frame, const std::uint64_t campaign_generation) {
  require_owner();
  bind_generation(campaign_generation);
  auto current = context(frame);
  const auto currency = sovereign_currency_for_civilization(
      current.world.civilizations, current.player.id);
  std::vector<NativeSettlementMissionView> result;
  for (const auto &fleet : current.world.fleets) {
    if (!fleet.is_active || fleet.civilization_id != current.player.id ||
        fleet.role != FleetRole::Colony ||
        fleet.embarked_population_millions <= 0.)
      continue;
    const auto outpost =
        ResourceOutpostOpportunityPlanner::is_outpost_fleet(fleet);
    NativeSettlementMissionView view;
    view.campaign_generation = campaign_generation;
    view.revision = next_revision_++;
    view.player_civilization_id = current.player.id;
    view.fleet_id = fleet.id;
    view.mission_order_revision = fleet.mission_order_revision;
    view.kind = outpost ? NativeSettlementMissionKind::ResourceOutpost
                        : NativeSettlementMissionKind::Colony;
    view.design_id = fleet.design_id;
    view.destination_system_id = fleet.destination_system_id;
    view.destination_body_id = fleet.destination_planetary_body_id;
    view.settlement_body_id = fleet.settlement_body_id;
    view.settlement_days_completed = fleet.settlement_days_completed;
    view.establishment_days = ColonizationSimulation::establishment_days(fleet);
    view.currency = currency;
    const auto authorization =
        outpost
            ? ColonizationSimulation::resource_outpost_expedition_authorization(
                  fleet)
            : ColonizationSimulation::colony_expedition_authorization(fleet);
    view.requires_new_authorization = authorization.is_new_expedition;
    view.authorization_budget_units = authorization.charge_budget_units;
    view.treasury_budget_units = current.economy.credits;
    view.funded = !view.requires_new_authorization ||
                  current.economy.credits + .0001 >=
                      view.authorization_budget_units;
    view.formatted_authorization =
        currency.format(view.authorization_budget_units);
    view.formatted_treasury = currency.format(current.economy.credits);
    if (outpost) {
      const auto plan = current.runtime.core().get_resource_outpost_opportunity_plan(
          &current.simulation, fleet.id, 8);
      view.fleet_name = plan.fleet_name;
      view.personnel_species_id = plan.personnel_species_id;
      view.personnel_species_name = plan.personnel_species_name;
      view.personnel_millions = plan.personnel_millions;
      view.can_receive_orders = plan.can_receive_orders;
      view.status = plan.status;
      for (const auto &candidate : plan.candidates)
        view.candidates.push_back(copy(candidate));
    } else {
      const auto plan = current.runtime.core().get_colony_opportunity_plan(
          &current.simulation, fleet.id, 8);
      view.fleet_name = plan.fleet_name;
      view.personnel_species_id = plan.passenger_species_id;
      view.personnel_species_name = plan.passenger_species_name;
      view.personnel_millions = plan.embarked_population_millions;
      view.can_receive_orders = plan.can_receive_orders;
      view.status = plan.status;
      for (const auto &candidate : plan.candidates)
        view.candidates.push_back(copy(candidate));
    }
    result.push_back(std::move(view));
  }
  std::ranges::sort(result, {}, &NativeSettlementMissionView::fleet_id);
  projected_ = result;
  return result;
}

NativeSettlementCommandOutcome NativeSettlementMissionController::issue(
    CampaignFrame &frame, const std::uint64_t campaign_generation,
    const std::uint64_t revision, const int fleet_id,
    const int destination_system_id, const int body_id) {
  require_owner();
  if (!generation_ || *generation_ != campaign_generation)
    return {false,
            "The campaign changed; refresh settlement opportunities first.",
            0};
  const auto *prior = find_projection(projected_, revision, fleet_id);
  if (!prior ||
      !find_candidate(*prior, destination_system_id, body_id))
    return {false,
            "That settlement opportunity is stale; refresh before ordering.",
            0};

  auto current = context(frame);
  if (prior->player_civilization_id != current.player.id)
    return {false,
            "The campaign changed; refresh settlement opportunities first.",
            0};
  const auto fleet = std::ranges::find(current.world.fleets, fleet_id,
                                        &FleetState::id);
  if (fleet == current.world.fleets.end() || !fleet->is_active ||
      fleet->civilization_id != current.player.id ||
      fleet->role != FleetRole::Colony)
    return {false, "That owned settlement vessel is no longer available.", 0};
  const auto outpost =
      ResourceOutpostOpportunityPlanner::is_outpost_fleet(*fleet);
  const auto expected_kind = outpost
                                 ? NativeSettlementMissionKind::ResourceOutpost
                                 : NativeSettlementMissionKind::Colony;
  const auto current_currency = sovereign_currency_for_civilization(
      current.world.civilizations, current.player.id);
  const auto current_authorization =
      outpost
          ? ColonizationSimulation::resource_outpost_expedition_authorization(
                *fleet)
          : ColonizationSimulation::colony_expedition_authorization(*fleet);
  if (prior->kind != expected_kind || prior->design_id != fleet->design_id ||
      prior->mission_order_revision != fleet->mission_order_revision ||
      prior->personnel_species_id !=
          fleet->embarked_population_species_id.value_or("") ||
      prior->personnel_millions != fleet->embarked_population_millions ||
      prior->requires_new_authorization !=
          current_authorization.is_new_expedition ||
      prior->authorization_budget_units !=
          current_authorization.charge_budget_units ||
      !same_currency(prior->currency, current_currency))
    return {false,
            "The settlement vessel or authorization changed; refresh first.",
            fleet->mission_order_revision};

  bool candidate_is_current = false;
  if (outpost) {
    const auto plan = current.runtime.core().get_resource_outpost_opportunity_plan(
        &current.simulation, fleet_id, 8);
    candidate_is_current = std::ranges::any_of(
        plan.candidates, [&](const auto &candidate) {
          return candidate.system_id == destination_system_id &&
                 candidate.planetary_body_id == body_id;
        });
  } else {
    const auto plan = current.runtime.core().get_colony_opportunity_plan(
        &current.simulation, fleet_id, 8);
    candidate_is_current = std::ranges::any_of(
        plan.candidates, [&](const auto &candidate) {
          return candidate.system_id == destination_system_id &&
                 candidate.planetary_body_id == body_id;
        });
  }
  if (!candidate_is_current)
    return {false,
            "That destination is no longer an available settlement opportunity.",
            fleet->mission_order_revision};

  const auto result = outpost
                          ? current.runtime.core()
                                .issue_resource_outpost_fleet_order(
                                    &current.simulation, current.player.id,
                                    fleet_id, destination_system_id, body_id)
                          : current.runtime.core().issue_colony_fleet_order(
                                &current.simulation, current.player.id, fleet_id,
                                destination_system_id, body_id);
  const auto updated = std::ranges::find(current.world.fleets, fleet_id,
                                          &FleetState::id);
  if (result.accepted) projected_.clear();
  return {result.accepted, result.message,
          updated == current.world.fleets.end()
              ? 0
              : updated->mission_order_revision};
}

} // namespace stellar::native_colony
