#include "native_missions.hpp"
#include <algorithm>
#include <cmath>
#include <ranges>
#include <unordered_set>
#include "stellar/core/colonization_runtime.hpp"
#include "stellar/core/colony_biology.hpp"
#include "stellar/core/colony_economy.hpp"
#include "stellar/core/colony_operations.hpp"
#include "stellar/core/detail/legacy_number_format.hpp"
#include "stellar/core/exploration_advance.hpp"
#include "stellar/core/fleet_transit.hpp"
#include "stellar/core/knowledge.hpp"
#include "stellar/core/lane_network.hpp"
#include "stellar/core/settlement_knowledge.hpp"
#include "stellar/core/species_environment.hpp"
#include "stellar/core/survey_operations.hpp"

namespace stellar::native_missions {
namespace {

// Chrome strings resolve through the shared catalog when bound; the literal
// stays the fallback for missing keys (and for unlocalized consumers like
// the mission tests).
std::string mt(const stellar::engine::LocalizationTable *locale,
               std::string_view key, std::string_view fallback) {
  if (locale && locale->contains(key))
    return std::string(locale->translate(key));
  return std::string(fallback);
}
std::string mtf(const stellar::engine::LocalizationTable *locale,
                std::string_view key, std::string_view arg,
                std::string_view fallback) {
  if (locale && locale->contains(key)) {
    const std::string args[]{std::string(arg)};
    return locale->format(key, args);
  }
  std::string text(fallback);
  const auto at = text.find("{0}");
  if (at != std::string::npos) text.replace(at, 3, arg);
  return text;
}


using namespace stellar::core;
using native_map::Color;
using native_map::FilledRectangle;
using native_map::Point;
using native_map::StrokedRectangle;
using native_map::Text;
using native_map::UiRect;

std::string fixed(double value, int minimum, int maximum) {
  return detail::legacy_custom_fixed(value, minimum, maximum);
}

// Reference ExplorationMissionStatus internal record.
struct MissionStatus {
  NativeMissionPhase phase{NativeMissionPhase::awaiting_order};
  std::optional<double> transit_days, survey_days, mission_days;
  std::string summary;
};

const StellarSystem *find_system(const FreshCampaignState &campaign, int id) {
  const auto found = std::ranges::find(campaign.systems, id, &StellarSystem::id);
  return found == campaign.systems.end() ? nullptr : &*found;
}

const PlanetaryBody *find_body(const FreshCampaignState &campaign, int id) {
  const auto found = std::ranges::find(campaign.bodies, id, &PlanetaryBody::id);
  return found == campaign.bodies.end() ? nullptr : &*found;
}

// Reference OperatingCapacity.
double operating_capacity(const FreshCampaignState &campaign,
                          int civilization_id) {
  const auto economy =
      std::ranges::find(campaign.economies, civilization_id,
                        &CivilizationEconomy::civilization_id);
  const double value =
      economy != campaign.economies.end()
          ? economy->last_base_operations_funding_fraction
          : 1.0;
  return std::isfinite(value) ? std::clamp(value, 0.0, 1.0) : 0.0;
}

// Reference ResolveSettlementBody: an explicit destination body is honored
// only when the colony assessment still permits founding; otherwise the
// resolver falls back to the best available surveyed world.
const PlanetaryBody *resolve_settlement_body(
    const FreshCampaignState &campaign, const FleetState &fleet, int system_id,
    const std::string &species_id) {
  if (fleet.destination_planetary_body_id) {
    const auto *body = find_body(campaign, *fleet.destination_planetary_body_id);
    if (!body || body->system_id != system_id)
      return nullptr;
    return species_colonization_assessment(species_id, *body)
                       .can_found_current_colony
                   ? body
                   : nullptr;
  }
  const SettlementKnowledgeWorldView world{
      campaign.systems, campaign.bodies, campaign.civilizations,
      campaign.colonies, campaign.fleets, campaign.knowledge};
  const auto resolved = resolve_best_available_settlement_body(
      world, fleet.civilization_id, system_id, species_id);
  return resolved ? find_body(campaign, *resolved) : nullptr;
}

// Reference ExplorationMissionStatus.Build.
MissionStatus evaluate(const FreshCampaignState &campaign,
                       const FleetState &fleet) {
  const int civilization_id = campaign.player_civilization_id;
  const double capacity = operating_capacity(campaign, civilization_id);
  const auto *knowledge = &campaign.knowledge;

  const auto awaiting = [](std::string summary) {
    MissionStatus status;
    status.summary = std::move(summary);
    return status;
  };

  if (!fleet.is_active)
    return awaiting(fleet.name + " is not an active mission fleet.");

  if (capacity <= 1e-7)
    return awaiting(fleet.name +
                    " is suspended because fleet operations are unfunded. "
                    "Restore the operating budget to resume its existing "
                    "mission.");

  if (fleet.hold_requested) {
    if (fleet.current_system_id) {
      const auto *system = find_system(campaign, *fleet.current_system_id);
      const std::string name =
          system ? system->name : "the current system";
      return awaiting(fleet.name + " is held at " + name +
                      "; resume to continue its existing mission.");
    }
    const std::optional<int> next_stop =
        !fleet.planned_route_system_ids.empty()
            ? std::optional<int>{fleet.planned_route_system_ids.front()}
            : fleet.destination_system_id;
    const auto *system =
        next_stop ? find_system(campaign, *next_stop) : nullptr;
    const std::string name = system ? system->name : "its next stop";
    MissionStatus status;
    status.phase = NativeMissionPhase::traveling;
    status.summary = fleet.name + " is holding after reaching " + name +
                     "; resume to continue its existing mission.";
    return status;
  }

  if (fleet.destination_system_id) {
    const auto *target = find_system(campaign, *fleet.destination_system_id);
    if (!target)
      return awaiting(fleet.name + " references an unknown destination system.");

    const double distance =
        measure_remaining_fleet_route(&campaign.systems, &fleet)
            .distance_light_years;
    const double speed = fleet.strategic_speed;
    const double local_days =
        fleet_remaining_chart_distance(campaign.systems, fleet) /
        fleet_local_transit_rate(fleet);
    std::optional<double> transit_days;
    if (speed > 0.0 && std::isfinite(distance))
      transit_days =
          std::max(0.0, (distance / speed + local_days) / capacity);

    std::optional<double> survey_days;
    const auto survey_level = knowledge->system_survey_level(
        civilization_id, target->id);
    if (fleet.role == FleetRole::Science &&
        survey_level >= SystemSurveyLevel::partially_surveyed) {
      const auto profile = SurveyOperationsProfiler{}.build(
          campaign.systems, campaign.bodies, target->id);
      survey_days = std::max(
          0.0,
          profile.estimated_science_survey_days *
                  (1.0 - knowledge->system_survey_progress(civilization_id,
                                                         target->id)) /
                  capacity);
    }

    std::optional<double> mission_days;
    switch (fleet.role) {
      case FleetRole::Science:
        if (transit_days && survey_days)
          mission_days = *transit_days + *survey_days;
        break;
      case FleetRole::Colony:
        if (transit_days)
          mission_days =
              *transit_days +
              (fleet.prevent_automatic_settlement
                   ? 0.0
                   : ColonizationSimulation::establishment_days(fleet) / capacity);
        break;
      case FleetRole::Scout:
        if (transit_days)
          mission_days =
              *transit_days +
              (survey_level < SystemSurveyLevel::partially_surveyed
                   ? ExplorationSimulation::scout_reconnaissance_days / capacity
                   : 0.0);
        break;
      default:
        mission_days = transit_days;
        break;
    }

    const std::string eta =
        transit_days
            ? "approximately " + fixed(*transit_days, 0, 1) +
                  " transit days remain"
            : "transit ETA is unavailable";
    std::string follow_up;
    if (survey_days)
      follow_up = "; approximately " + fixed(*survey_days, 0, 1) +
                  " known detailed-survey days remain after arrival";
    else if (fleet.role == FleetRole::Science)
      follow_up = "; detailed-survey duration remains unknown until "
                  "reconnaissance establishes system complexity";

    std::string destination = target->name;
    if (fleet.role == FleetRole::Colony &&
        fleet.destination_planetary_body_id) {
      const auto *body =
          find_body(campaign, *fleet.destination_planetary_body_id);
      if (body && body->system_id == target->id)
        destination = body->name + " in " + target->name;
    }

    MissionStatus status;
    status.phase = NativeMissionPhase::traveling;
    status.transit_days = transit_days;
    status.survey_days = survey_days;
    status.mission_days = mission_days;
    status.summary = fleet.name + " is traveling to " + destination + "; " +
                     eta + follow_up + ".";
    return status;
  }

  const StellarSystem *system =
      fleet.current_system_id ? find_system(campaign, *fleet.current_system_id)
                              : nullptr;
  if (!system)
    return awaiting(fleet.name + " is awaiting mission orders.");

  switch (fleet.role) {
    case FleetRole::Scout: {
      const auto level =
          knowledge->system_survey_level(civilization_id, system->id);
      if (level < SystemSurveyLevel::partially_surveyed) {
        const double completed =
            fleet.reconnaissance_system_id == system->id
                ? fleet.reconnaissance_days_completed
                : 0.0;
        const double remaining =
            std::max(0.0,
                     (ExplorationSimulation::scout_reconnaissance_days -
                      completed) /
                         capacity);
        MissionStatus status;
        status.phase = NativeMissionPhase::scouting;
        status.mission_days = remaining;
        status.summary = fleet.name + " is scouting " + system->name +
                         "; approximately " + fixed(remaining, 1, 1) +
                         " game days remain.";
        return status;
      }
      return awaiting(fleet.name +
                      " has completed reconnaissance-grade work in " +
                      system->name + " and is awaiting another order.");
    }
    case FleetRole::Science: {
      if (knowledge->is_system_fully_surveyed(civilization_id, system->id))
        return awaiting(fleet.name + " has completed the detailed survey of " +
                        system->name + " and is awaiting another order.");
      std::optional<double> survey_days;
      if (knowledge->system_survey_level(civilization_id, system->id) >=
          SystemSurveyLevel::partially_surveyed) {
        const auto profile = SurveyOperationsProfiler{}.build(
            campaign.systems, campaign.bodies, system->id);
        survey_days = std::max(
            0.0,
            profile.estimated_science_survey_days *
                    (1.0 - knowledge->system_survey_progress(civilization_id,
                                                           system->id)) /
                    capacity);
      }
      const std::string estimate =
          survey_days
              ? "approximately " + fixed(*survey_days, 0, 1) +
                    " detailed-survey days remain"
              : "survey duration is not yet known; the first science pass "
                "will establish reconnaissance-grade complexity";
      MissionStatus status;
      status.phase = NativeMissionPhase::science_survey;
      status.survey_days = survey_days;
      status.mission_days = survey_days;
      status.summary = fleet.name + " is conducting a detailed survey of " +
                       system->name + "; " + estimate + ".";
      return status;
    }
    case FleetRole::Colony: {
      if (fleet.embarked_population_millions <= 0.0)
        return awaiting(fleet.name +
                        " is not carrying colonists and has no active colony "
                        "mission.");
      if (fleet.prevent_automatic_settlement)
        return awaiting(fleet.name + " is on station in " + system->name +
                        "; select a surveyed world to authorize settlement.");
      if (fleet.settlement_body_id) {
        const double remaining = std::max(
            0.0,
            (ColonizationSimulation::establishment_days(fleet) -
             fleet.settlement_days_completed) /
                capacity);
        const auto *body = find_body(campaign, *fleet.settlement_body_id);
        const std::string site = body ? body->name : "settlement";
        MissionStatus status;
        status.phase = NativeMissionPhase::establishing_colony;
        status.mission_days = remaining;
        status.summary = "Establishing " + site + ": approximately " +
                         fixed(remaining, 1, 1) +
                         " game days remain. Habitats and services are under "
                         "construction.";
        return status;
      }
      const std::string species_id =
          fleet.embarked_population_species_id.value_or("");
      const auto species = std::ranges::find_if(
          species_environment_profiles(),
          [&](const auto &profile) { return profile.id == species_id; });
      if (species_id.empty() ||
          species == species_environment_profiles().end())
        return awaiting(fleet.name +
                        " carries population without a valid passenger "
                        "species identity.");
      if (!knowledge->is_system_fully_surveyed(civilization_id, system->id))
        return awaiting(fleet.name + " is carrying " +
                        fixed(fleet.embarked_population_millions, 0, 1) +
                        " million " + species->display_name + " colonists in " +
                        system->name +
                        ", but a completed science survey is still required "
                        "before settlement.");
      if (std::ranges::find(campaign.colonies, system->id,
                            &Colony::system_id) != campaign.colonies.end())
        return awaiting(fleet.name + " is carrying colonists in " +
                        system->name +
                        ", but that system already contains a founded colony "
                        "under the current single-colony early-release "
                        "model.");
      const auto *candidate =
          resolve_settlement_body(campaign, fleet, system->id, species_id);
      if (!candidate)
        return awaiting(fleet.name + " is carrying " +
                        fixed(fleet.embarked_population_millions, 0, 1) +
                        " million " + species->display_name + " colonists in " +
                        system->name +
                        ", but no surveyed body is currently viable for that "
                        "population.");
      const auto assessment =
          species_colonization_assessment(species_id, *candidate);
      const char *viability =
          assessment.viability ==
                  SpeciesColonizationViability::NaturallyViable
              ? "naturally viable"
              : "currently supported by the prototype "
                "habitat-compatibility fallback";
      const double establish_days =
          ColonizationSimulation::establishment_days(fleet);
      MissionStatus status;
      status.phase = NativeMissionPhase::establishing_colony;
      status.mission_days = establish_days / capacity;
      status.summary =
          fleet.name + " has arrived at " + candidate->name + " in " +
          system->name + "; the world is " + viability + " for " +
          species->display_name + " and establishment requires " +
          fixed(establish_days, 0, 0) + " game days.";
      return status;
    }
    default:
      return awaiting(fleet.name + " is awaiting mission orders.");
  }
}

// Reference FormatColonizationViability.
std::string_view viability_label(SpeciesColonizationViability value) noexcept {
  switch (value) {
    case SpeciesColonizationViability::NaturallyViable:
      return "natural";
    case SpeciesColonizationViability::HabitatSupportedFallback:
      return "habitat support";
    default:
      return "unsuitable";
  }
}

// Reference LimitingFactor.ToString() — the enum name verbatim.
std::string_view limiting_factor_label(EnvironmentalLimitingFactor value) noexcept {
  switch (value) {
    case EnvironmentalLimitingFactor::None: return "None";
    case EnvironmentalLimitingFactor::Gravity: return "Gravity";
    case EnvironmentalLimitingFactor::Temperature: return "Temperature";
    case EnvironmentalLimitingFactor::Pressure: return "Pressure";
    case EnvironmentalLimitingFactor::Atmosphere: return "Atmosphere";
    case EnvironmentalLimitingFactor::Solvent: return "Solvent";
    case EnvironmentalLimitingFactor::Immersion: return "Immersion";
    case EnvironmentalLimitingFactor::Radiation: return "Radiation";
  }
  return "None";
}

// .NET P0 and N0 formats used by the reference detail builders.
std::string percent0(double value) { return fixed(value * 100.0, 0, 0) + "%"; }

std::string thousands0(double value) {
  std::string digits = fixed(std::fabs(value), 0, 0);
  for (int i = static_cast<int>(digits.size()) - 3; i > 0; i -= 3)
    digits.insert(static_cast<std::size_t>(i), ",");
  return (value < 0.0 ? "-" : "") + digits;
}

// Reference CompactPlannerReason.
std::string compact_reason(std::string_view reason) {
  constexpr std::size_t maximum = 180;
  if (reason.size() <= maximum)
    return std::string(reason);
  std::string trimmed(reason.substr(0, maximum - 1));
  while (!trimmed.empty() && trimmed.back() == ' ')
    trimmed.pop_back();
  return trimmed + "…";
}

}  // namespace

// Reference GetUiColonyOpportunityState + BuildFleetOnlyDetails /
// BuildSelectedSiteDetails / GetUiResourceOutpostOpportunityState.
NativeColonySiteSelection colony_site_selection(
    std::span<const native_colony::NativeSettlementMissionView> fleets,
    int requested_fleet_index, int requested_site_index) {
  NativeColonySiteSelection selection;
  selection.action_label = "Fund & Settle";
  if (fleets.empty()) {
    selection.details =
        "No populated colony ships are currently available for settlement "
        "planning.";
    selection.status = selection.details;
    return selection;
  }
  selection.available = true;
  selection.fleet_count = static_cast<int>(fleets.size());
  selection.fleet_index =
      std::clamp(requested_fleet_index, 0, selection.fleet_count - 1);
  const auto &view = fleets[selection.fleet_index];
  selection.fleet_id = view.fleet_id;
  selection.outpost =
      view.kind == native_colony::NativeSettlementMissionKind::ResourceOutpost;
  const std::string aboard =
      (view.personnel_species_name.empty()
           ? ""
           : view.personnel_species_name + " - ") +
      fixed(view.personnel_millions, 0, 1) +
      (selection.outpost ? "M specialists aboard" : "M aboard");
  const std::string heading =
      (selection.outpost ? "Outpost vessel " : "Colony ship ") +
      std::to_string(selection.fleet_index + 1) + "/" +
      std::to_string(selection.fleet_count) + ": " + view.fleet_name + " - " +
      aboard;

  if (!view.can_receive_orders || view.candidates.empty()) {
    selection.details = heading + "\n\n" + view.status;
    selection.status = view.status;
    selection.action_label =
        selection.outpost ? "Fund & Deploy" : "Fund & Settle";
    return selection;
  }

  selection.site_count = static_cast<int>(view.candidates.size());
  selection.site_index =
      std::clamp(requested_site_index, 0, selection.site_count - 1);
  const auto &site = view.candidates[selection.site_index];
  selection.site_system_id = site.system_id;
  selection.site_body_id = site.body_id;
  // Reference canAfford: credits + epsilon >= the fixed expedition cost.
  const bool affordable =
      view.treasury_budget_units + .0001 >= view.authorization_budget_units;
  selection.can_settle = site.can_order && affordable;

  std::string details = heading + "\n";
  if (selection.outpost) {
    details += "Resource site " + std::to_string(selection.site_index + 1) +
               "/" + std::to_string(selection.site_count) +
               (site.can_order ? ": OK " : ": - ") + site.system_name + " / " +
               site.body_name + "\n" + site.deposit_grade + " " +
               site.deposit_material_name + " | yield " +
               fixed(site.extraction_yield_multiplier, 2, 2) + "x | access " +
               percent0(site.deposit_accessibility) + " | reserve " +
               thousands0(site.initial_deposit_materials) + "\n" +
               "Natural fit " + percent0(site.natural_habitability) +
               " | unprotected capacity " +
               percent0(site.unprotected_operational_capacity) +
               " | limiting factor " +
               std::string(limiting_factor_label(site.limiting_factor)) +
               "\nSealed outpost authorization: " +
               view.formatted_authorization + "\nTreasury available: " +
               view.formatted_treasury + " - " +
               (affordable ? "funded" : "additional funding required") +
               "\n\n" + compact_reason(site.reason);
    selection.action_label = "Fund & Deploy";
    selection.status =
        site.can_order && !affordable
            ? "Outpost deployment requires " + view.formatted_authorization +
                  "; " + view.formatted_treasury + " is available."
            : site.reason;
    return selection;
  }

  details += "Site " + std::to_string(selection.site_index + 1) + "/" +
             std::to_string(selection.site_count) +
             (site.can_order ? ": OK " : ": - ") + site.system_name + " / " +
             site.body_name + "\nViability: " +
             std::string(viability_label(site.viability)) +
             " | natural fit " + percent0(site.natural_habitability) +
             " | unprotected " +
             percent0(site.unprotected_operational_capacity) + "\n" +
             "Limiting factor: " +
             std::string(limiting_factor_label(site.limiting_factor)) +
             " | reach: " +
             (site.reach.is_supported ? "supported" : "blocked") +
             (site.reach.is_authoritative ? "" : " (provisional)") + "\n" +
             "Expedition authorization: " + view.formatted_authorization +
             "\nTreasury available: " + view.formatted_treasury + " - " +
             (affordable ? "funded" : "additional funding required") +
             "\n\n" + compact_reason(site.reason);
  selection.details = std::move(details);
  selection.status =
      site.can_order && !affordable
          ? "Settlement requires " + view.formatted_authorization + "; " +
                view.formatted_treasury + " is available."
          : site.reason;
  return selection;
}

const FleetState *find_available_freighter(const FreshCampaignState &campaign) {
  // Reference FindAvailableFreighter (Main.Surface.cs).
  std::unordered_set<int> developed;
  for (const auto &colony : campaign.colonies)
    if (colony.civilization_id == campaign.player_civilization_id &&
        colony.kind == SettlementKind::Colony)
      developed.insert(colony.system_id);
  const FleetState *best = nullptr;
  for (const auto &fleet : campaign.fleets) {
    if (!fleet.is_active ||
        fleet.civilization_id != campaign.player_civilization_id ||
        fleet.role != FleetRole::Logistics ||
        fleet.design_id != "bulk_freighter" || fleet.destination_system_id ||
        fleet.freight_home_colony_id || fleet.freight_target_outpost_id ||
        fleet.cargo_materials > 0.0 || !fleet.current_system_id ||
        !developed.contains(*fleet.current_system_id))
      continue;
    if (!best || fleet.id < best->id) best = &fleet;
  }
  return best;
}

std::vector<NativeMissionColonyRow>
build_owned_colony_rows(const FreshCampaignState &campaign) {
  std::vector<const Colony *> owned;
  for (const auto &colony : campaign.colonies)
    if (colony.civilization_id == campaign.player_civilization_id)
      owned.push_back(&colony);
  std::ranges::sort(owned, {}, &Colony::id);
  const auto *freighter = find_available_freighter(campaign);
  std::vector<NativeMissionColonyRow> rows;
  for (const auto *colony : owned) {
    NativeMissionColonyRow row;
    row.colony_id = colony->id;
    row.planetary_body_id = colony->planetary_body_id.value_or(-1);
    row.name = colony->name;
    const auto *body =
        colony->planetary_body_id
            ? find_body(campaign, *colony->planetary_body_id)
            : nullptr;
    row.planet_name = body ? body->name : "Orbital habitat";
    const auto *system = find_system(campaign, colony->system_id);
    row.system_name = system ? system->name : "Deep space";
    row.population_millions = colony->population_millions;
    row.is_resource_outpost = colony->kind == SettlementKind::ResourceOutpost;
    row.can_land = body && body->environment.has_solid_surface;
    // Reference UiOwnedColonySnapshot freight gating.
    const auto outpost =
        resource_outpost_snapshot(campaign.bodies, campaign.economies,
                                  *colony);
    const bool has_material =
        outpost.stored_materials > 0.0 || outpost.extraction_per_day > 0.0;
    row.can_request_freight =
        outpost.is_resource_outpost && freighter && has_material;
    if (outpost.is_resource_outpost) {
      if (!freighter)
        row.freight_reason =
            "Build an Interstellar Bulk Freighter and station it at a "
            "developed colony.";
      else if (!has_material)
        row.freight_reason = outpost.status;
      else
        row.freight_reason =
            "Dispatch " + freighter->name + " to collect up to " +
            fixed(freighter->cargo_material_capacity, 0, 1) +
            " material units.";
    }
    rows.push_back(std::move(row));
  }
  return rows;
}

NativeMissionBoard build_mission_board(const FreshCampaignState &campaign) {
  NativeMissionBoard board;
  const int player_id = campaign.player_civilization_id;
  // Reference ActiveMissions: IsActive && owned && mission role, ordered by id.
  std::vector<const FleetState *> active;
  for (const auto &fleet : campaign.fleets) {
    if (!fleet.is_active || fleet.civilization_id != player_id)
      continue;
    if (fleet.role != FleetRole::Scout && fleet.role != FleetRole::Science &&
        fleet.role != FleetRole::Colony)
      continue;
    active.push_back(&fleet);
  }
  std::ranges::sort(active, {}, [](const FleetState *fleet) {
    return fleet->id;
  });
  for (const auto *fleet : active) {
    if (board.missions.size() >= 8)
      break;
    const auto status = evaluate(campaign, *fleet);
    NativeMissionCard card;
    card.fleet_id = fleet->id;
    card.role = fleet->role;
    card.phase = status.phase;
    card.fleet_name = fleet->name;
    const auto destination_id = fleet->destination_system_id
                                    ? fleet->destination_system_id
                                    : fleet->current_system_id;
    const auto *system =
        destination_id ? find_system(campaign, *destination_id) : nullptr;
    card.destination = system ? system->name : "Deep space";
    card.eta = status.mission_days
                   ? fixed(*status.mission_days, 1, 1) + " days remaining"
               : status.phase == NativeMissionPhase::awaiting_order
                   ? "Ready for orders"
                   : "ETA unavailable";
    card.summary = status.summary;
    board.missions.push_back(std::move(card));
  }
  return board;
}

std::string_view mission_phase_label(NativeMissionPhase phase) noexcept {
  switch (phase) {
    case NativeMissionPhase::awaiting_order:
      return "Awaiting order";
    case NativeMissionPhase::traveling:
      return "Traveling";
    case NativeMissionPhase::scouting:
      return "Scouting";
    case NativeMissionPhase::science_survey:
      return "Science survey";
    case NativeMissionPhase::establishing_colony:
      return "Establishing colony";
  }
  return "Awaiting order";
}

Color mission_phase_color(NativeMissionPhase phase) noexcept {
  // Reference MissionColor keys on the rendered phase label; "Scouting" and
  // "Establishing colony" fall through to gold there, matching here.
  switch (phase) {
    case NativeMissionPhase::traveling:
      return {154, 225, 255, 255};
    case NativeMissionPhase::science_survey:
      return {180, 160, 228, 255};
    default:
      return {232, 199, 102, 255};
  }
}

namespace {

std::string_view role_label(FleetRole role) noexcept {
  switch (role) {
    case FleetRole::Scout:
      return "Scout";
    case FleetRole::Science:
      return "Science";
    case FleetRole::Colony:
      return "Colony";
    default:
      return "Fleet";
  }
}

}  // namespace

[[nodiscard]] std::optional<UiRect>
clip_rect(UiRect rect, const UiRect &clip) {
  const auto x1 = std::max(rect.x, clip.x);
  const auto y1 = std::max(rect.y, clip.y);
  const auto x2 = std::min(rect.x + rect.width, clip.x + clip.width);
  const auto y2 = std::min(rect.y + rect.height, clip.y + clip.height);
  if (x2 <= x1 || y2 <= y1) return std::nullopt;
  return UiRect{x1, y1, x2 - x1, y2 - y1};
}

MissionLayout mission_layout_for(const NativeMissionBoard &board,
                                 const NativeColonySiteSelection &selection,
                                 std::size_t colony_rows, int width,
                                 int height, bool show_sites,
                                 float scroll_offset) {
  MissionLayout layout;
  const auto sw = static_cast<float>(width),
             sh = static_cast<float>(height);
  layout.scale = std::min(sw / 1600.f, sh / 900.f);
  const auto scale = layout.scale;
  layout.heading_font_pixels = std::max(14, static_cast<int>(18.f * scale));
  layout.body_font_pixels = std::max(10, static_cast<int>(13.f * scale));
  layout.small_font_pixels = std::max(9, static_cast<int>(11.f * scale));
  layout.panel = {std::max(112.f * scale, sw - 470.f * scale), 78.f * scale,
                  std::min(450.f * scale, sw - 128.f * scale),
                  std::min(560.f * scale, sh - 210.f * scale)};
  const auto pad = 12.f * scale;
  layout.header = {layout.panel.x + pad, layout.panel.y + pad,
                   layout.panel.width - pad * 2.f, 30.f * scale};
  layout.close_button = {layout.panel.x + layout.panel.width - pad -
                             26.f * scale,
                         layout.header.y, 26.f * scale, 26.f * scale};
  // Reference Missions / Colony Sites tabs.
  const auto tab_top = layout.header.y + layout.header.height + 6.f * scale;
  const auto tab_width = (layout.panel.width - pad * 2.f - 8.f * scale) * .5f;
  const auto tab_height = 26.f * scale;
  layout.missions_tab = {layout.panel.x + pad, tab_top, tab_width, tab_height};
  layout.sites_tab = {layout.missions_tab.x + tab_width + 8.f * scale, tab_top,
                      tab_width, tab_height};
  auto top = tab_top + tab_height + 10.f * scale;

  if (!show_sites) {
    layout.list_viewport = {layout.panel.x + pad, top,
                            layout.panel.width - pad * 2.f,
                            layout.panel.y + layout.panel.height - pad - top};
    const auto card_height = 78.f * scale;
    const auto card_gap = 8.f * scale;
    for (std::size_t i = 0; i < board.missions.size(); ++i) {
      layout.cards.push_back(
          {layout.panel.x + pad, top - scroll_offset,
           layout.panel.width - pad * 2.f, card_height});
      top += card_height + card_gap;
    }
    if (board.missions.empty())
      layout.empty_hint = {layout.panel.x + pad, top,
                           layout.panel.width - pad * 2.f, 60.f * scale};
    return layout;
  }

  // Colony Sites tab: fleet/site navigation, details, select-ship, owned
  // colony rows (reference RefreshContent + RefreshOwnedColonies).
  const auto nav_height = 24.f * scale;
  const auto nav_gap = 6.f * scale;
  const auto nav_width =
      (layout.panel.width - pad * 2.f - nav_gap) * .5f;
  layout.previous_fleet = {layout.panel.x + pad, top, nav_width, nav_height};
  layout.next_fleet = {layout.previous_fleet.x + nav_width + nav_gap, top,
                       nav_width, nav_height};
  top += nav_height + 4.f * scale;
  layout.previous_site = {layout.panel.x + pad, top, nav_width, nav_height};
  layout.next_site = {layout.previous_site.x + nav_width + nav_gap, top,
                      nav_width, nav_height};
  top += nav_height + 8.f * scale;
  layout.details = {layout.panel.x + pad, top,
                    layout.panel.width - pad * 2.f, 150.f * scale};
  top += layout.details.height + 6.f * scale;
  layout.select_ship = {layout.panel.x + pad, top,
                        layout.panel.width - pad * 2.f, 30.f * scale};
  top += layout.select_ship.height + 6.f * scale;
  layout.action_status = {layout.panel.x + pad, top,
                          layout.panel.width - pad * 2.f, 30.f * scale};
  top += layout.action_status.height + 6.f * scale;
  layout.list_viewport = {layout.panel.x + pad, top,
                          layout.panel.width - pad * 2.f,
                          layout.panel.y + layout.panel.height - pad - top};
  const auto row_height = 52.f * scale;
  for (std::size_t i = 0; i < colony_rows; ++i) {
    UiRect row{layout.panel.x + pad, top - scroll_offset,
               layout.panel.width - pad * 2.f, row_height};
    layout.colony_rows.push_back(row);
    // Reference owned-colony card header actions: View, Land, Collect.
    const auto button_h = 26.f * scale, button_y = row.y + 13.f * scale,
               gap = 6.f * scale, narrow = 56.f * scale;
    UiRect collect{row.x + row.width - 6.f * scale - 68.f * scale, button_y,
                   68.f * scale, button_h};
    UiRect land{collect.x - gap - narrow, button_y, narrow, button_h};
    layout.colony_view_buttons.push_back(
        {land.x - gap - narrow, button_y, narrow, button_h});
    layout.colony_land_buttons.push_back(land);
    layout.colony_collect_buttons.push_back(collect);
    top += row_height + 6.f * scale;
  }
  (void)selection;
  return layout;
}

std::vector<MissionFocusTarget> mission_focus_targets(
    const MissionLayout &layout, const NativeColonySiteSelection &selection,
    bool show_sites, std::span<const NativeMissionColonyRow> colonies,
    const stellar::engine::LocalizationTable *locale) {
  std::vector<MissionFocusTarget> targets;
  targets.push_back(
      {layout.missions_tab, mt(locale, "MISSIONS_TAB_MISSIONS", "Missions")});
  targets.push_back(
      {layout.sites_tab, mt(locale, "MISSIONS_TAB_SITES", "Colony sites")});
  targets.push_back({layout.close_button, mt(locale, "MISSIONS_CLOSE", "Close")});
  if (!show_sites) {
    std::ranges::sort(targets, [](const MissionFocusTarget &a,
                                  const MissionFocusTarget &b) {
      return a.bounds.y == b.bounds.y ? a.bounds.x < b.bounds.x
                                      : a.bounds.y < b.bounds.y;
    });
    return targets;
  }
  if (selection.fleet_index > 0)
    targets.push_back({layout.previous_fleet,
                       mt(locale, "MISSIONS_PREV_SHIP", "Previous ship")});
  if (selection.fleet_index < selection.fleet_count - 1)
    targets.push_back({layout.next_fleet,
                       mt(locale, "MISSIONS_NEXT_SHIP", "Next ship")});
  if (selection.site_index > 0)
    targets.push_back({layout.previous_site,
                       mt(locale, "MISSIONS_PREV_SITE", "Previous site")});
  if (selection.site_index < selection.site_count - 1)
    targets.push_back({layout.next_site,
                       mt(locale, "MISSIONS_NEXT_SITE", "Next site")});
  if (selection.fleet_id)
    targets.push_back({layout.select_ship,
                       mt(locale, "MISSIONS_FOCUS_SELECT_SHIP",
                          "Select ship on map")});
  // Colony-row buttons live in the scroll viewport: a clipped button stays
  // in the ring with its visible band so keyboard navigation can scroll it
  // fully into view.
  for (std::size_t i = 0;
       i < layout.colony_rows.size() && i < colonies.size(); ++i) {
    const auto push_row_button = [&](const native_map::UiRect &button,
                                     std::string label) {
      const auto clipped = clip_rect(button, layout.list_viewport);
      if (clipped)
        targets.push_back({*clipped, std::move(label), button});
    };
    push_row_button(
        layout.colony_view_buttons[i],
        mtf(locale, "MISSIONS_FOCUS_VIEW", colonies[i].name, "View {0}"));
    if (colonies[i].can_land)
      push_row_button(
          layout.colony_land_buttons[i],
          mtf(locale, "MISSIONS_FOCUS_LAND", colonies[i].name, "Land {0}"));
    if (colonies[i].is_resource_outpost)
      push_row_button(
          layout.colony_collect_buttons[i],
          mtf(locale, "MISSIONS_FOCUS_COLLECT", colonies[i].name,
              "Collect {0}"));
  }
  std::ranges::sort(targets, [](const MissionFocusTarget &a,
                                const MissionFocusTarget &b) {
    return a.bounds.y == b.bounds.y ? a.bounds.x < b.bounds.x
                                    : a.bounds.y < b.bounds.y;
  });
  return targets;
}

std::string NativeMissionView::focused_label(
    const NativeMissionBoard &board,
    std::span<const native_colony::NativeSettlementMissionView> fleets,
    std::span<const NativeMissionColonyRow> colonies, int width,
    int height) const {
  if (!visible_ || focus_ < 0) return {};
  const auto selection = colony_site_selection(fleets, fleet_index_, site_index_);
  const auto layout = mission_layout_for(board, selection, colonies.size(),
                                         width, height, show_sites_,
                                         scroll_.scroll_offset);
  const auto targets = mission_focus_targets(layout, selection, show_sites_, colonies, locale_);
  return focus_ < static_cast<int>(targets.size())
             ? targets[static_cast<std::size_t>(focus_)].label
             : std::string{};
}

std::optional<native_map::UiRect> NativeMissionView::focused_bounds(
    const NativeMissionBoard &board,
    std::span<const native_colony::NativeSettlementMissionView> fleets,
    std::span<const NativeMissionColonyRow> colonies, int width,
    int height) const {
  if (!visible_ || focus_ < 0) return std::nullopt;
  const auto selection = colony_site_selection(fleets, fleet_index_, site_index_);
  const auto layout = mission_layout_for(board, selection, colonies.size(),
                                         width, height, show_sites_,
                                         scroll_.scroll_offset);
  const auto targets = mission_focus_targets(layout, selection, show_sites_, colonies, locale_);
  return focus_ < static_cast<int>(targets.size())
             ? std::optional<native_map::UiRect>{
                   targets[static_cast<std::size_t>(focus_)].bounds}
             : std::nullopt;
}

MissionViewCommand NativeMissionView::handle(
    const native_map::InputEvent &event, const NativeMissionBoard &board,
    std::span<const native_colony::NativeSettlementMissionView> fleets,
    std::span<const NativeMissionColonyRow> colonies, int width, int height) {
  MissionViewCommand command;
  if (!visible_)
    return command;
  const auto selection =
      colony_site_selection(fleets, fleet_index_, site_index_);
  const auto layout =
      mission_layout_for(board, selection, colonies.size(), width, height,
                         show_sites_, scroll_.scroll_offset);
  // Keyboard focus contract: Tab/arrows walk the actionable controls in
  // (y,x) order, Home/End jump to the ends, Return/Space activate through
  // the same press/release dispatch a pointer click takes. Escape releases
  // a live ring (the host closes the panel on a second press); pointer
  // presses and cancels reset the ring.
  if (event.type == native_map::InputEventType::PointerCancelled) {
    focus_ = -1;
    command.captured = true;
    return command;
  }
  if (event.type == native_map::InputEventType::EscapePressed) {
    if (focus_ >= 0) {
      focus_ = -1;
      command.captured = true;
    }
    return command;
  }
  if (event.type == native_map::InputEventType::KeyPressed && event.key) {
    constexpr std::uint32_t kTab = 9u, kReturn = 13u, kSpace = 32u;
    constexpr std::uint32_t kRight = 0x4000004fu, kLeft = 0x40000050u,
                            kDown = 0x40000051u, kUp = 0x40000052u;
    constexpr std::uint32_t kHome = 0x4000004au, kEnd = 0x4000004du;
    const auto targets =
        mission_focus_targets(layout, selection, show_sites_, colonies, locale_);
    const int count = static_cast<int>(targets.size());
    const bool fwd = (event.key == kTab && !event.shift) ||
                     event.key == kRight || event.key == kDown;
    const bool bwd = (event.key == kTab && event.shift) ||
                     event.key == kLeft || event.key == kUp;
    // Focus-follow: snap a clipped row button fully into the scroll viewport.
    const auto snap_focus_into_view = [&] {
      if (focus_ < 0 || focus_ >= count) return;
      const auto &target = targets[static_cast<std::size_t>(focus_)];
      if (!target.unclipped) return;
      const auto extent =
          static_cast<float>(colonies.size()) * (52.f + 6.f) * layout.scale;
      scroll_.sync(extent, layout.list_viewport.height);
      scroll_.scroll_interval_into_view(target.unclipped->y,
                                        target.unclipped->y +
                                            target.unclipped->height,
                                        layout.list_viewport.y,
                                        layout.list_viewport.y +
                                            layout.list_viewport.height);
    };
    // Edge scroll: the ring only covers rows whose buttons intersect the
    // viewport, so a row showing <13px contributes nothing. When nav lands
    // on the ring's list edge and the scroll has room, advance the content
    // window by one row instead of wrapping — the recomputed edge target is
    // the newly revealed row's button.
    const auto row_pitch = 58.f * layout.scale;
    const auto list_extent =
        static_cast<float>(colonies.size()) * row_pitch;
    const auto ring_at_scroll = [&] {
      const auto next_layout = mission_layout_for(
          board, selection, colonies.size(), width, height, show_sites_,
          scroll_.scroll_offset);
      return mission_focus_targets(next_layout, selection, show_sites_,
                                   colonies, locale_);
    };
    const auto first_row_index = [&] {
      for (int i = 0; i < count; ++i)
        if (targets[static_cast<std::size_t>(i)].unclipped) return i;
      return count;
    };
    if (count > 0 && (event.key == kHome || event.key == kEnd)) {
      // Jump to the true list ends, not just the clipped-visible rows.
      scroll_.sync(list_extent, layout.list_viewport.height);
      scroll_.scroll_to(event.key == kHome ? 0.f : scroll_.max_scroll());
      const auto jumped = ring_at_scroll();
      focus_ = event.key == kHome ? 0
                                  : static_cast<int>(jumped.size()) - 1;
      command.captured = true;
      return command;
    }
    if (count > 0 && (fwd || bwd)) {
      if (focus_ >= 0 && focus_ < count) {
        scroll_.sync(list_extent, layout.list_viewport.height);
        if (fwd && focus_ == count - 1 &&
            targets.back().unclipped &&
            scroll_.scroll_offset < scroll_.max_scroll()) {
          scroll_.scroll_by(row_pitch);
          const auto scrolled = ring_at_scroll();
          focus_ = static_cast<int>(scrolled.size()) - 1;
          command.captured = true;
          return command;
        }
        const int first_row = first_row_index();
        if (bwd && focus_ == first_row && first_row < count &&
            scroll_.scroll_offset > 0.f) {
          scroll_.scroll_by(-row_pitch);
          const auto scrolled = ring_at_scroll();
          focus_ = -1;
          for (int i = 0; i < static_cast<int>(scrolled.size()); ++i)
            if (scrolled[static_cast<std::size_t>(i)].unclipped &&
                focus_ < 0)
              focus_ = i;
          if (focus_ < 0) focus_ = 0;
          command.captured = true;
          return command;
        }
      }
      focus_ = focus_ < 0 || focus_ >= count
                   ? (bwd ? count - 1 : 0)
                   : (focus_ + (bwd ? -1 : 1) + count) % count;
      snap_focus_into_view();
      command.captured = true;
      return command;
    }
    if ((event.key == kReturn || event.key == kSpace) && focus_ >= 0 &&
        focus_ < count) {
      const auto &rect = targets[static_cast<std::size_t>(focus_)].bounds;
      native_map::InputEvent press{native_map::InputEventType::LeftPressed};
      press.position = {rect.x + rect.width * .5f,
                        rect.y + rect.height * .5f};
      auto release = press;
      release.type = native_map::InputEventType::LeftReleased;
      const int keep = focus_;
      (void)handle(press, board, fleets, colonies, width, height);
      auto activated = handle(release, board, fleets, colonies, width, height);
      if (visible_)
        focus_ = keep;
      activated.captured = true;
      return activated;
    }
    // Unhandled keys keep falling through to global shortcuts.
    return command;
  }
  if (event.type == native_map::InputEventType::Wheel &&
      layout.panel.contains(event.position)) {
    // Wheel scrolls the active tab's list (mission cards or colony rows).
    const auto extent =
        show_sites_
            ? static_cast<float>(colonies.size()) * (52.f + 6.f) * layout.scale
            : static_cast<float>(board.missions.size()) * (78.f + 8.f) *
                  layout.scale;
    scroll_.sync(extent, layout.list_viewport.height);
    scroll_.scroll_by(-event.wheel_y * 42.f * layout.scale);
    command.captured = true;
    return command;
  }
  if (event.type == native_map::InputEventType::LeftReleased &&
      layout.close_button.contains(event.position)) {
    command.kind = MissionViewCommandKind::Close;
    command.captured = true;
    return command;
  }
  if (event.type != native_map::InputEventType::LeftReleased ||
      !layout.panel.contains(event.position)) {
    // ContainPointerInput: pointer input inside the panel never reaches the
    // map. A press also releases the keyboard ring.
    if ((event.type == native_map::InputEventType::LeftPressed ||
         event.type == native_map::InputEventType::RightPressed ||
         event.type == native_map::InputEventType::RightReleased ||
         event.type == native_map::InputEventType::Wheel ||
         event.type == native_map::InputEventType::PointerMove) &&
        layout.panel.contains(event.position)) {
      if (event.type == native_map::InputEventType::LeftPressed ||
          event.type == native_map::InputEventType::RightPressed)
        focus_ = -1;
      command.captured = true;
    }
    return command;
  }
  command.captured = true;
  if (layout.missions_tab.contains(event.position)) {
    show_sites_ = false;
    scroll_ = {};
    return command;
  }
  if (layout.sites_tab.contains(event.position)) {
    show_sites_ = true;
    scroll_ = {};
    return command;
  }
  if (show_sites_) {
    if (layout.previous_fleet.contains(event.position) &&
        selection.fleet_index > 0) {
      --fleet_index_;
      site_index_ = 0;
      return command;
    }
    if (layout.next_fleet.contains(event.position) &&
        selection.fleet_index < selection.fleet_count - 1) {
      ++fleet_index_;
      site_index_ = 0;
      return command;
    }
    if (layout.previous_site.contains(event.position) &&
        selection.site_index > 0) {
      --site_index_;
      return command;
    }
    if (layout.next_site.contains(event.position) &&
        selection.site_index < selection.site_count - 1) {
      ++site_index_;
      return command;
    }
    // Reference "Select ship on map": focuses the colony ship; the settle
    // order itself is issued from the destination system view.
    if (layout.select_ship.contains(event.position) && selection.fleet_id) {
      command.kind = MissionViewCommandKind::FocusFleet;
      command.fleet_id = *selection.fleet_id;
      return command;
    }
    for (std::size_t i = 0;
         i < layout.colony_view_buttons.size() && i < colonies.size(); ++i) {
      // Row buttons are clipped to the scroll viewport: a click outside the
      // visible band must not reach a scrolled-away control.
      if (!layout.list_viewport.contains(event.position))
        break;
      if (layout.colony_view_buttons[i].contains(event.position)) {
        command.kind = MissionViewCommandKind::OpenColony;
        command.colony_id = colonies[i].colony_id;
        return command;
      }
      if (colonies[i].can_land &&
          layout.colony_land_buttons[i].contains(event.position)) {
        command.kind = MissionViewCommandKind::LandColony;
        command.colony_id = colonies[i].colony_id;
        return command;
      }
      // Reference Collect: always clickable on outposts — a denied request
      // surfaces its reason through the status channel.
      if (colonies[i].is_resource_outpost &&
          layout.colony_collect_buttons[i].contains(event.position)) {
        command.kind = MissionViewCommandKind::CollectOutpostFreight;
        command.colony_id = colonies[i].colony_id;
        return command;
      }
    }
  }
  return command;
}

void NativeMissionView::render(
    native_map::DrawList &out, const NativeMissionBoard &board,
    std::span<const native_colony::NativeSettlementMissionView> fleets,
    std::span<const NativeMissionColonyRow> colonies, int width,
    int height) const {
  if (!visible_)
    return;
  const auto selection =
      colony_site_selection(fleets, fleet_index_, site_index_);
  const auto layout =
      mission_layout_for(board, selection, colonies.size(), width, height,
                         show_sites_, scroll_.scroll_offset);
  const auto scale = layout.scale;
  const auto muted = Color{122, 154, 192, 255};
  const auto body = Color{190, 212, 236, 255};
  const auto accent = Color{154, 225, 255, 255};
  const auto gold = Color{232, 199, 102, 255};
  out.overlay.emplace_back(FilledRectangle{layout.panel, {10, 17, 31, 242}});
  out.overlay.emplace_back(
      StrokedRectangle{layout.panel, {72, 101, 145, 255}});
  out.overlay.emplace_back(Text{{layout.header.x, layout.header.y},
                                mt(locale_, "MISSIONS_TITLE",
                                   "MISSIONS & SETTLEMENT"),
                                Color{233, 242, 252, 255},
                                layout.heading_font_pixels});
  out.overlay.emplace_back(FilledRectangle{layout.close_button,
                                           {30, 41, 62, 220}});
  out.overlay.emplace_back(
      StrokedRectangle{layout.close_button, {96, 125, 168, 255}});
  out.overlay.emplace_back(Text{
      {layout.close_button.x + 8.f * scale,
       layout.close_button.y + 4.f * scale},
      "x", Color{212, 226, 244, 255}, layout.body_font_pixels});
  const auto tab_fill = [](bool active) {
    return active ? Color{24, 76, 71, 255} : Color{13, 51, 52, 255};
  };
  out.overlay.emplace_back(
      FilledRectangle{layout.missions_tab, tab_fill(!show_sites_)});
  out.overlay.emplace_back(
      StrokedRectangle{layout.missions_tab, {96, 125, 168, 255}});
  out.overlay.emplace_back(Text{{layout.missions_tab.x + 8.f * scale,
                                 layout.missions_tab.y + 5.f * scale},
                                mt(locale_, "MISSIONS_TAB_MISSIONS",
                                   "Missions"),
                                body, layout.body_font_pixels});
  out.overlay.emplace_back(
      FilledRectangle{layout.sites_tab, tab_fill(show_sites_)});
  out.overlay.emplace_back(
      StrokedRectangle{layout.sites_tab, {96, 125, 168, 255}});
  out.overlay.emplace_back(Text{{layout.sites_tab.x + 8.f * scale,
                                 layout.sites_tab.y + 5.f * scale},
                                mt(locale_, "MISSIONS_TAB_SITES",
                                   "Colony Sites"),
                                body, layout.body_font_pixels});
  const auto draw_focus_ring = [&] {
    if (focus_ < 0) return;
    const auto targets =
        mission_focus_targets(layout, selection, show_sites_, colonies, locale_);
    if (focus_ < static_cast<int>(targets.size()))
      out.overlay.emplace_back(
          StrokedRectangle{targets[static_cast<std::size_t>(focus_)].bounds,
                           {160, 210, 255, 255}});
  };

  if (!show_sites_) {
    if (board.missions.empty()) {
      out.overlay.emplace_back(Text{{layout.empty_hint.x, layout.empty_hint.y},
                                    mt(locale_, "MISSIONS_EMPTY",
                                       "No active mission fleets."),
                                    body, layout.body_font_pixels});
      out.overlay.emplace_back(Text{
          {layout.empty_hint.x, layout.empty_hint.y + 18.f * scale},
          mt(locale_, "MISSIONS_EMPTY_HINT",
             "Commission scout, science, or colony ships to begin."),
          muted, layout.small_font_pixels});
      draw_focus_ring();
      return;
    }
    for (std::size_t i = 0;
         i < layout.cards.size() && i < board.missions.size(); ++i) {
      const auto &card = board.missions[i];
      const auto &rect = layout.cards[i];
      const auto visible = clip_rect(rect, layout.list_viewport);
      if (!visible) continue;
      out.overlay.emplace_back(FilledRectangle{*visible, {17, 27, 47, 240}});
      out.overlay.emplace_back(StrokedRectangle{*visible, {59, 83, 118, 255}});
      const auto pad = 10.f * scale;
      auto line = rect.y + 8.f * scale;
      out.overlay.emplace_back(Text{{rect.x + pad, line}, card.fleet_name,
                                    accent, layout.body_font_pixels, 0.f,
                                    layout.list_viewport});
      const auto role_key = [](FleetRole role) {
        switch (role) {
          case FleetRole::Scout: return "FLEET_ROLE_SCOUT";
          case FleetRole::Science: return "FLEET_ROLE_SCIENCE";
          case FleetRole::Colony: return "FLEET_ROLE_COLONY";
          default: return "FLEET_ROLE_FLEET";
        }
      };
      const auto phase_key = [](NativeMissionPhase phase) {
        switch (phase) {
          case NativeMissionPhase::awaiting_order:
            return "MISSION_PHASE_AWAITING";
          case NativeMissionPhase::traveling:
            return "MISSION_PHASE_TRAVELING";
          case NativeMissionPhase::scouting:
            return "MISSION_PHASE_SCOUTING";
          case NativeMissionPhase::science_survey:
            return "MISSION_PHASE_SURVEY";
          case NativeMissionPhase::establishing_colony:
            return "MISSION_PHASE_COLONIZING";
        }
        return "MISSION_PHASE_AWAITING";
      };
      out.overlay.emplace_back(
          Text{{rect.x + pad, line + 17.f * scale},
               mt(locale_, role_key(card.role), role_label(card.role)) +
                   " - " +
                   mt(locale_, phase_key(card.phase),
                      mission_phase_label(card.phase)),
               mission_phase_color(card.phase), layout.small_font_pixels, 0.f,
               layout.list_viewport});
      line += 36.f * scale;
      out.overlay.emplace_back(
          Text{{rect.x + pad, line}, card.destination + " - " + card.eta, body,
               layout.small_font_pixels, 0.f, layout.list_viewport});
      out.overlay.emplace_back(
          Text{{rect.x + pad, line + 15.f * scale}, card.summary, muted,
               layout.small_font_pixels, rect.width - pad * 2.f,
               layout.list_viewport});
    }
    draw_focus_ring();
    return;
  }

  // Colony Sites tab.
  const auto nav_fill = Color{13, 51, 52, 255};
  const auto nav = [&](const UiRect &rect, std::string_view label,
                       bool enabled,
                       std::optional<UiRect> clip = std::nullopt) {
    const auto band =
        clip ? clip_rect(rect, *clip) : std::optional<UiRect>{rect};
    if (!band) return;
    out.overlay.emplace_back(FilledRectangle{*band, nav_fill});
    out.overlay.emplace_back(
        StrokedRectangle{*band, enabled ? Color{96, 125, 168, 255}
                                        : Color{45, 60, 82, 255}});
    out.overlay.emplace_back(
        Text{{rect.x + 8.f * scale, rect.y + 5.f * scale}, std::string(label),
             enabled ? body : muted, layout.small_font_pixels, 0.f, clip});
  };
  nav(layout.previous_fleet, mt(locale_, "MISSIONS_BTN_PREV_SHIP", "< Ship"),
      selection.fleet_index > 0);
  nav(layout.next_fleet, mt(locale_, "MISSIONS_BTN_NEXT_SHIP", "Ship >"),
      selection.fleet_index < selection.fleet_count - 1);
  nav(layout.previous_site, mt(locale_, "MISSIONS_BTN_PREV_SITE", "< Site"),
      selection.site_index > 0);
  nav(layout.next_site, mt(locale_, "MISSIONS_BTN_NEXT_SITE", "Site >"),
      selection.site_index < selection.site_count - 1);
  out.overlay.emplace_back(Text{{layout.details.x, layout.details.y},
                                selection.details, body,
                                layout.small_font_pixels,
                                layout.details.width});
  out.overlay.emplace_back(
      FilledRectangle{layout.select_ship,
                      selection.fleet_id ? Color{24, 76, 71, 255}
                                         : Color{24, 32, 46, 255}});
  out.overlay.emplace_back(
      StrokedRectangle{layout.select_ship,
                       selection.fleet_id ? Color{102, 232, 164, 255}
                                          : Color{45, 60, 82, 255}});
  out.overlay.emplace_back(
      Text{{layout.select_ship.x + 10.f * scale,
            layout.select_ship.y + 8.f * scale},
           selection.fleet_id
               ? mt(locale_, "MISSIONS_SELECT_SHIP", "SELECT SHIP ON MAP")
               : mt(locale_, "MISSIONS_NO_SHIP", "NO COLONY SHIP"),
           selection.fleet_id ? Color{233, 242, 252, 255} : muted,
           layout.body_font_pixels});
  if (!selection.status.empty() && selection.site_count > 0)
    out.overlay.emplace_back(
        Text{{layout.action_status.x, layout.action_status.y},
             selection.status, gold, layout.small_font_pixels,
             layout.action_status.width});
  if (colonies.empty())
    out.overlay.emplace_back(Text{{layout.panel.x + 12.f * scale,
                                   layout.action_status.y +
                                       layout.action_status.height +
                                       4.f * scale},
                                  mt(locale_, "MISSIONS_OWNED_WORLDS",
                                     "OWNED WORLDS"),
                                  accent, layout.small_font_pixels});
  for (std::size_t i = 0;
       i < layout.colony_rows.size() && i < colonies.size(); ++i) {
    const auto &row = colonies[i];
    const auto &rect = layout.colony_rows[i];
    const auto visible = clip_rect(rect, layout.list_viewport);
    if (!visible) continue;
    out.overlay.emplace_back(FilledRectangle{*visible, {17, 27, 47, 240}});
    out.overlay.emplace_back(StrokedRectangle{*visible, {59, 83, 118, 255}});
    out.overlay.emplace_back(
        Text{{rect.x + 10.f * scale, rect.y + 6.f * scale},
             row.name + "  /  " + row.planet_name + ", " + row.system_name,
             Color{233, 242, 252, 255}, layout.body_font_pixels, 0.f,
             layout.list_viewport});
    out.overlay.emplace_back(
        Text{{rect.x + 10.f * scale, rect.y + 24.f * scale},
             fixed(row.population_millions, 0, 1) + "M population",
             gold, layout.small_font_pixels, 0.f, layout.list_viewport});
    const auto &button = layout.colony_view_buttons[i];
    const auto button_visible = clip_rect(button, layout.list_viewport);
    if (button_visible) {
      out.overlay.emplace_back(
          FilledRectangle{*button_visible, {13, 51, 52, 255}});
      out.overlay.emplace_back(
          StrokedRectangle{*button_visible, {96, 125, 168, 255}});
      out.overlay.emplace_back(
          Text{{button.x + 10.f * scale, button.y + 6.f * scale},
               mt(locale_, "MISSIONS_VIEW", "View"), body,
               layout.small_font_pixels, 0.f, layout.list_viewport});
    }
    nav(layout.colony_land_buttons[i], mt(locale_, "MISSIONS_LAND", "Land"),
        row.can_land, layout.list_viewport);
    if (row.is_resource_outpost)
      nav(layout.colony_collect_buttons[i],
          mt(locale_, "MISSIONS_COLLECT", "Collect"), row.can_request_freight,
          layout.list_viewport);
  }
  draw_focus_ring();
}

}  // namespace stellar::native_missions
