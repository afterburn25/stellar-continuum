#include "adaptive_campaign_host.hpp"

#include <stellar/build_version.hpp>
#include <stellar/core/adaptive_research_campaign.hpp>
#include <stellar/core/detail/adaptive_research_outcome_snapshot_json.hpp>
#include <stellar/core/detail/adaptive_research_sha256.hpp>
#include <stellar/core/diplomacy_state.hpp>
#include <stellar/core/fleet_transit.hpp>
#include <stellar/core/integrated_adaptive_campaign.hpp>
#include <stellar/core/planetary_catalog.hpp>
#include <stellar/core/persistable_fresh_campaign.hpp>
#include <stellar/core/ship_designs.hpp>
#include <stellar/core/player_campaign_json.hpp>
#include <stellar/core/player_campaign_persistence.hpp>
#include <stellar/core/species_environment.hpp>
#include <stellar/engine/atomic_file_write.hpp>
#include <stellar/engine/runtime_paths.hpp>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <numeric>
#include <span>
#include <sstream>
#include <stdexcept>
#include <string>
#include <typeinfo>

using Json = nlohmann::json;
using namespace stellar::core;

namespace {

template <class Value>
Json optional_json(const std::optional<Value> &value) {
  return value ? Json(*value) : Json(nullptr);
}

template <class Enum>
Json optional_enum_json(const std::optional<Enum> &value) {
  return value ? Json(static_cast<int>(*value)) : Json(nullptr);
}

Json research_json(const IntegratedAdaptiveCampaignRuntime &runtime) {
  const AdaptiveResearchCampaignSnapshotCodec codec(runtime.research_runtime());
  const auto snapshot = codec.capture(runtime.research());
  Json civilizations = Json::array();
  for (const auto &civilization : snapshot.civilizations) {
    Json funding = Json::array();
    for (const auto &value : civilization.project_funding)
      funding.push_back({{"nodeId", value.node_id},
                         {"reservedMilestoneCredits",
                          value.reserved_milestone_credits},
                         {"consumedMilestoneCredits",
                          value.consumed_milestone_credits},
                         {"authorizationCredits", value.authorization_credits}});
    civilizations.push_back(
        {{"civilizationId", civilization.civilization_id},
         {"speciesId", civilization.species_id},
         {"referenceProfileId", civilization.reference_profile_id},
         {"applicabilityContextId", civilization.applicability_context_id},
         {"research",
          Json::parse(detail::encode_adaptive_research_snapshot_v5_dto(
              civilization.research))},
         {"projectFunding", std::move(funding)}});
  }
  return {{"schemaVersion", snapshot.schema_version},
          {"catalogId", snapshot.catalog_id},
          {"civilizations", std::move(civilizations)}};
}

Json diplomacy_json(IntegratedAdaptiveCampaignRuntime &runtime) {
  const auto snapshot = runtime.diplomacy().snapshot();
  Json result;
  result["contacts"] = Json::array();
  for (const auto &v : snapshot.contacts)
    result["contacts"].push_back(
        {{"observerCivilizationId", v.observer_civilization_id},
         {"contactId", v.contact_id},
         {"targetCivilizationId", optional_json(v.target_civilization_id)},
         {"firstObservedTick", v.first_observed_tick},
         {"lastObservedTick", v.last_observed_tick},
         {"lastObservedSystemId", optional_json(v.last_observed_system_id)},
         {"awareness", static_cast<int>(v.awareness)},
         {"condition", static_cast<int>(v.condition)},
         {"communicationAvailable", v.communication_available},
         {"confidence", v.confidence}});
  result["relationships"] = Json::array();
  for (const auto &v : snapshot.relationships) {
    Json grievances = Json::array();
    for (const auto &g : v.grievances)
      grievances.push_back({{"createdAtTick", g.created_at_tick},
                            {"sourceCivilizationId", g.source_civilization_id},
                            {"severity", g.severity}, {"reason", g.reason}});
    result["relationships"].push_back(
        {{"civilizationAId", v.civilization_a_id},
         {"civilizationBId", v.civilization_b_id},
         {"politicalState", static_cast<int>(v.political_state)},
         {"trust", v.trust},
         {"hostility", v.hostility}, {"fear", v.fear},
         {"respect", v.respect}, {"cooperation", v.cooperation},
         {"grievances", std::move(grievances)}});
  }
  result["accessPermissions"] = Json::array();
  for (const auto &v : snapshot.access_permissions)
    result["accessPermissions"].push_back(
        {{"grantorCivilizationId", v.grantor_civilization_id},
         {"visitorCivilizationId", v.visitor_civilization_id},
         {"permission", static_cast<int>(v.permission)},
         {"updatedAtTick", v.updated_at_tick}});
  result["claims"] = Json::array();
  for (const auto &v : snapshot.claims)
    result["claims"].push_back(
        {{"claimId", v.claim_id}, {"claimantCivilizationId", v.claimant_civilization_id},
         {"systemId", v.system_id}, {"assertedAtTick", v.asserted_at_tick},
         {"active", v.active}, {"knownToCivilizationIds", v.known_to_civilization_ids}});
  result["claimResponses"] = Json::array();
  for (const auto &v : snapshot.claim_responses)
    result["claimResponses"].push_back(
        {{"claimId", v.claim_id}, {"respondingCivilizationId", v.responding_civilization_id},
         {"response", static_cast<int>(v.response)},
         {"respondedAtTick", v.responded_at_tick}});
  result["agreements"] = Json::array();
  for (const auto &v : snapshot.agreements)
    result["agreements"].push_back(
        {{"agreementId", v.agreement_id}, {"civilizationAId", v.civilization_a_id},
         {"civilizationBId", v.civilization_b_id},
         {"type", static_cast<int>(v.type)},
         {"status", static_cast<int>(v.status)},
         {"startedAtTick", v.started_at_tick},
         {"endedAtTick", optional_json(v.ended_at_tick)},
         {"externalTermsReference",
          optional_json(v.external_terms_reference)}});
  result["proposals"] = Json::array();
  for (const auto &v : snapshot.proposals)
    result["proposals"].push_back(
        {{"proposalId", v.proposal_id}, {"proposerCivilizationId", v.proposer_civilization_id},
         {"recipientCivilizationId", v.recipient_civilization_id},
         {"kind", static_cast<int>(v.kind)},
         {"agreementType", optional_enum_json(v.agreement_type)},
         {"status", static_cast<int>(v.status)},
         {"createdAtTick", v.created_at_tick},
         {"resolvedAtTick", optional_json(v.resolved_at_tick)},
         {"summary", v.summary},
         {"externalTermsReference",
          optional_json(v.external_terms_reference)}});
  result["recentHistory"] = Json::array();
  for (const auto &v : snapshot.recent_history)
    result["recentHistory"].push_back(
        {{"eventId", v.event_id}, {"tick", v.tick}, {"kind", v.kind},
         {"primaryCivilizationId", v.primary_civilization_id},
         {"secondaryCivilizationId",
          optional_json(v.secondary_civilization_id)},
         {"systemId", optional_json(v.system_id)},
         {"summary", v.summary},
         {"knownToCivilizationIds", v.known_to_civilization_ids}});
  result["nextClaimId"] = snapshot.next_claim_id;
  result["nextAgreementId"] = snapshot.next_agreement_id;
  result["nextProposalId"] = snapshot.next_proposal_id;
  result["nextEventId"] = snapshot.next_event_id;
  result["lastProcessedTick"] =
      runtime.diplomacy_runtime().last_processed_tick();
  result["nextMaintenanceReviewTick"] =
      runtime.diplomacy_runtime().next_maintenance_review_tick();
  return result;
}

Json intelligence_json(const IntegratedAdaptiveCampaignRuntime &runtime) {
  Json result = Json::array();
  for (const auto &v : runtime.combat_intelligence())
    result.push_back({{"observerId", v.observer_id}, {"fleetId", v.fleet_id},
                      {"power", v.power}, {"observedDay", v.observed_day},
                      {"evidence", v.evidence}});
  return result;
}

Json adaptive_diagnostic(IntegratedAdaptiveCampaignRuntime &runtime,
                         const CampaignDiagnosticBuilder &world_diagnostic) {
  Json result = world_diagnostic(runtime.world().campaign());
  result["format"] = "stellar-adaptive-campaign-simulation-diagnostic-v1";
  result["phase"] = "adaptive-campaign-after-simulation-steps";
  result["research"] = research_json(runtime);
  result["diplomacy"] = diplomacy_json(runtime);
  result["combatIntelligence"] = intelligence_json(runtime);
  return result;
}

std::string state_hash(std::string_view text) {
  const auto digest = detail::adaptive_research_sha256(
      {reinterpret_cast<const std::uint8_t *>(text.data()), text.size()});
  std::ostringstream result;
  result << std::hex << std::setfill('0');
  for (const auto byte : digest) result << std::setw(2) << static_cast<int>(byte);
  return result.str();
}

void write_new_file(const std::filesystem::path &output, const Json &value) {
  if (output.empty())
    return;
  auto pending = output;
  pending += ".pending";
  if (std::filesystem::exists(output))
    throw std::runtime_error("Refusing to overwrite catalog output: " +
                             output.string());
  bool owns_pending = false;
  try {
    std::ofstream stream(pending, std::ios::out | std::ios::noreplace);
    if (!stream)
      throw std::runtime_error("Refusing to overwrite catalog output: " +
                               output.string());
    owns_pending = true;
    stream << value.dump() << '\n';
    stream.flush();
    if (!stream)
      throw std::runtime_error("Catalog output write failed: " +
                               pending.string());
    stream.close();
    if (!stream)
      throw std::runtime_error("Catalog output close failed: " +
                               pending.string());
    std::filesystem::rename(pending, output);
    owns_pending = false;
  } catch (...) {
    if (owns_pending) {
      std::error_code cleanup_error;
      std::filesystem::remove(pending, cleanup_error);
    }
    throw;
  }
}

AdaptiveResearchStrategicRuntime load_runtime(
    const std::filesystem::path &research_root) {
  try {
    return load_adaptive_research_strategic_runtime(research_root);
  } catch (const std::exception &error) {
    throw std::runtime_error(
        "Cannot initialize Adaptive Research from '" + research_root.string() +
        "' [" + typeid(error).name() + "]: " + error.what());
  }
}

// Late-game stress: give every warp-capable civilization extra active military
// fleets mirroring starter_fleet's field invariants. Even-indexed squadrons are
// in interstellar transit toward Sol so movement and sensor/contact phases run
// real work each tick; the rest hold at home.
void inject_stress_fleets(FreshCampaignState &world, int per_civilization) {
  if (per_civilization <= 0)
    return;
  const auto &design = ship_design_for_role(FleetRole::Military);
  const auto contested =
      std::ranges::find(world.systems, sol_system_id, &StellarSystem::id);
  int next_id = 0;
  for (const auto &fleet : world.fleets)
    next_id = std::max(next_id, fleet.id + 1);
  for (const auto &civilization : world.civilizations) {
    if (civilization.development_stage ==
        CivilizationDevelopmentStage::PreWarp)
      continue;
    const auto home = std::ranges::find(world.systems,
                                        civilization.home_system_id,
                                        &StellarSystem::id);
    if (home == world.systems.end())
      continue;
    for (int index = 0; index < per_civilization; ++index, ++next_id) {
      if (next_id == std::numeric_limits<int>::max())
        throw std::overflow_error("Fleet identity space is exhausted.");
      FleetState fleet;
      fleet.id = next_id;
      fleet.civilization_id = civilization.id;
      fleet.name =
          civilization.name + " Squadron " + std::to_string(index + 1);
      fleet.role = design.role;
      fleet.design_id = design.id;
      fleet.strategic_speed = design.strategic_speed;
      fleet.maximum_leg_range_light_years =
          design.maximum_leg_range_light_years;
      fleet.fuel_capacity_light_years = design.fuel_endurance_light_years;
      fleet.fuel_remaining_light_years = design.fuel_endurance_light_years;
      fleet.sensor_range = design.sensor_range;
      fleet.is_active = true;
      if (index % 2 == 0 && contested != world.systems.end() &&
          home->id != contested->id) {
        fleet.transit_phase = FleetTransitPhase::InterstellarWarp;
        fleet.transit_origin_system_id = home->id;
        fleet.transit_target_system_id = contested->id;
        fleet.destination_system_id = contested->id;
        fleet.planned_route_system_ids = {contested->id};
        fleet.transit_progress =
            0.05 + 0.9 * static_cast<double>(index) /
                       std::max(1, per_civilization);
        fleet.position =
            interpolate_chart_position(*home, *contested, fleet.transit_progress);
      } else {
        fleet.position = {home->position.x, home->position.y};
        fleet.current_system_id = home->id;
      }
      world.fleets.push_back(std::move(fleet));
    }
  }
}

} // namespace

int run_adaptive_campaign_host(
    const AdaptiveCampaignHostOptions &options,
    std::span<const CatalogStar> stellar_catalog,
    CampaignDiagnosticBuilder campaign_diagnostic) {
  if (!campaign_diagnostic)
    throw std::invalid_argument(
        "Adaptive campaign diagnostic builder is required");
  if (options.repeats < 1 || options.repeats > 10 ||
      options.simulation_ticks < 1 || options.simulation_ticks > 10000 ||
      !std::isfinite(options.step_days) || options.step_days <= 0.0 ||
      !std::isfinite(options.step_days * options.simulation_ticks) ||
      !std::isfinite(options.step_days * options.simulation_ticks *
                     options.repeats))
    throw std::invalid_argument(
        "Adaptive campaign ticks, repeats and step days are outside bounds");
  if ((options.systems != 250 && options.systems != 500 &&
       options.systems != 1000 && options.systems != 2500) ||
      options.pre_warp_civilizations < 1 ||
      options.pre_warp_civilizations > 13 ||
      options.ancient_civilizations < 0 ||
      options.ancient_civilizations > 3)
    throw std::invalid_argument(
        "Adaptive campaign systems or civilization counts are outside bounds");
  if (options.autosave_every < 0 ||
      options.autosave_every > options.simulation_ticks)
    throw std::invalid_argument(
        "Adaptive campaign autosave interval must be 0..ticks");
  if (options.stress_fleets < 0 || options.stress_fleets > 100000)
    throw std::invalid_argument(
        "Adaptive campaign stress fleets must be 0..100000 per civilization");
  (void)species_environment_profile(options.player_species);
  const auto asset_root = std::filesystem::absolute(
      options.asset_root.empty() ? stellar::engine::executable_directory()
                                 : options.asset_root);
  const auto research_root = std::filesystem::absolute(
      asset_root / "Data/research/v1");
  std::string retained_state;
  Json final_diagnostic;
  Json phase_timings = Json::array();
  std::size_t fleet_count = 0;
  bool advanced = true;
  std::uint64_t sensor_contacts = 0, adaptive_events = 0,
                diplomacy_events = 0, industry_allocations = 0,
                construction_events = 0, shipbuilding_events = 0,
                exploration_events = 0, combat_events = 0,
                colonization_events = 0;
  double initialization_total_ms = 0.0, step_total_ms = 0.0;
  std::vector<double> step_times;
  std::vector<double> autosave_times;
  std::size_t autosave_bytes = 0;
  const auto autosave_path =
      std::filesystem::temp_directory_path() /
      ("stellar-adaptive-autosave-" + std::to_string(options.seed) + ".json");

  for (int repeat = 0; repeat < options.repeats; ++repeat) {
    const auto initialization_started = std::chrono::steady_clock::now();
    auto fresh_world = seed_persistable_fresh_campaign(
        options.seed, stellar_catalog,
        {"2050-03-21T00:00:00Z", options.systems,
         options.pre_warp_civilizations, options.ancient_civilizations,
         options.player_species});
    inject_stress_fleets(fresh_world, options.stress_fleets);
    auto runtime = IntegratedAdaptiveCampaignRuntime::create_fresh(
        load_runtime(research_root), std::move(fresh_world));
    runtime.set_profiling_enabled(true);
    const auto &world = runtime.world().campaign();
    const auto research_civilizations =
        runtime.research().civilization_ids();
    if (research_civilizations.size() != world.civilizations.size() ||
        std::ranges::any_of(world.civilizations, [&](const auto &civilization) {
          return runtime.research().try_get_civilization(civilization.id) ==
                 nullptr;
        }))
      throw std::runtime_error(
          "Integrated Adaptive campaign did not initialize every civilization");
    initialization_total_ms += std::chrono::duration<double, std::milli>(
                                   std::chrono::steady_clock::now() -
                                   initialization_started)
                                   .count();
    const auto initial_state =
        adaptive_diagnostic(runtime, campaign_diagnostic).dump();
    for (int tick = 0; tick < options.simulation_ticks; ++tick) {
      const auto step_started = std::chrono::steady_clock::now();
      const auto result = runtime.advance(
          options.step_days, static_cast<double>(tick + 1) * options.step_days);
      const auto elapsed = std::chrono::duration<double, std::milli>(
                               std::chrono::steady_clock::now() - step_started)
                               .count();
      step_times.push_back(elapsed);
      step_total_ms += elapsed;
      if (!result.core.research_events.empty())
        throw std::runtime_error(
            "Integrated Adaptive campaign unexpectedly advanced legacy research");
      for (const auto &entry : result.sensor_contacts)
        sensor_contacts += static_cast<std::uint64_t>(entry.recorded_contacts);
      adaptive_events += result.research_events.size();
      diplomacy_events += static_cast<std::uint64_t>(
          result.diplomacy.processed_diplomacy_events());
      industry_allocations += result.core.industry_allocations.size();
      construction_events += result.core.construction_events.size();
      shipbuilding_events += result.core.shipbuilding_events.size();
      exploration_events += result.core.exploration_events.size();
      combat_events += result.core.combat_events.size();
      colonization_events += result.core.colonization_events.size();
      if (options.autosave_every > 0 &&
          (tick + 1) % options.autosave_every == 0) {
        const auto save_started = std::chrono::steady_clock::now();
        const auto save_json = encode_player_campaign_v17_json(
            capture_player_campaign_v17(
                runtime, {(tick + 1) * options.step_days,
                          "adaptive-benchmark", "2050-03-21T00:00:00Z"}));
        stellar::engine::write_file_atomically(
            autosave_path, std::as_bytes(std::span(save_json)));
        autosave_bytes = save_json.size();
        autosave_times.push_back(std::chrono::duration<double, std::milli>(
                                     std::chrono::steady_clock::now() -
                                     save_started)
                                     .count());
      }
    }
    final_diagnostic = adaptive_diagnostic(runtime, campaign_diagnostic);
    fleet_count = runtime.world().campaign().fleets.size();
    phase_timings = Json::array();
    for (const auto &sample : runtime.performance_samples())
      phase_timings.push_back(
          {{"phase", std::string(sample.phase)},
           {"samples", sample.timing.samples},
           {"totalMs", sample.timing.total_nanoseconds / 1e6},
           {"meanMs", sample.timing.samples
                          ? sample.timing.total_nanoseconds / 1e6 /
                                static_cast<double>(sample.timing.samples)
                          : 0.0},
           {"maxMs", sample.timing.maximum_nanoseconds / 1e6}});
    const auto serialized = final_diagnostic.dump();
    advanced = advanced && serialized != initial_state;
    if (repeat == 0)
      retained_state = serialized;
    else if (retained_state != serialized)
      throw std::runtime_error(
          "Adaptive campaign final state differs between repeats");
  }

  const auto final_hash = state_hash(retained_state);
  final_diagnostic["simulation"] =
      {{"ticks", options.simulation_ticks}, {"stepDays", options.step_days},
       {"totalSimulatedDays", options.simulation_ticks * options.step_days},
       {"repeatCount", options.repeats}, {"stateHash", final_hash}};
  write_new_file(options.output, final_diagnostic);
  std::sort(step_times.begin(), step_times.end());
  const auto percentile =
      static_cast<std::size_t>(std::ceil(step_times.size() * .95)) - 1;
  std::sort(autosave_times.begin(), autosave_times.end());
  const double autosave_total_ms =
      std::accumulate(autosave_times.begin(), autosave_times.end(), 0.0);
  if (options.autosave_every > 0) {
    std::error_code ignored;
    std::filesystem::remove(autosave_path, ignored);
  }
  Json report = {
      {"mode", "adaptive-campaign-simulation-benchmark"},
      {"engineVersion", STELLAR_ENGINE_VERSION},
      {"sourceCommit", STELLAR_SOURCE_COMMIT},
      {"gameplayParity", false},
      {"playerSaveCompatible", false},
      {"scope", "integrated native Adaptive Research campaign diagnostic"},
      {"seed", options.seed}, {"systems", options.systems},
      {"ticksPerRepeat", options.simulation_ticks},
      {"stepDays", options.step_days}, {"repeats", options.repeats},
      {"totalSimulatedDays", options.simulation_ticks * options.step_days *
                                  options.repeats},
      {"initializationTotalMs", initialization_total_ms},
      {"initializationMeanMs", initialization_total_ms / options.repeats},
      {"stepTotalMs", step_total_ms},
      {"stepMeanMs", step_total_ms / step_times.size()},
      {"stepP95Ms", step_times[percentile]}, {"stepPeakMs", step_times.back()},
      {"autosaveIntervalTicks", options.autosave_every},
      {"autosaveCount", autosave_times.size()},
      {"autosaveMeanMs", autosave_times.empty()
                             ? 0.0
                             : autosave_total_ms / autosave_times.size()},
      {"autosaveP95Ms", autosave_times.empty()
                            ? 0.0
                            : autosave_times[static_cast<std::size_t>(
                                  std::ceil(autosave_times.size() * .95)) -
                                1]},
      {"autosavePeakMs",
       autosave_times.empty() ? 0.0 : autosave_times.back()},
      {"autosaveBytes", autosave_bytes},
      {"stressFleetsPerCivilization", options.stress_fleets},
      {"phaseTimings", phase_timings},
      {"sensorContactsRecorded", sensor_contacts},
      {"adaptiveResearchEvents", adaptive_events},
      {"diplomacyEvents", diplomacy_events},
      {"outputRecordCounts",
       {{"industryAllocations", industry_allocations},
        {"constructionEvents", construction_events},
        {"shipbuildingEvents", shipbuilding_events},
        {"legacyResearchEvents", 0},
        {"adaptiveResearchEvents", adaptive_events},
        {"explorationEvents", exploration_events},
        {"combatEvents", combat_events},
        {"colonizationEvents", colonization_events},
        {"diplomacyEvents", diplomacy_events},
        {"sensorContactsRecorded", sensor_contacts}}},
      {"finalStateCounts",
       {{"researchCivilizations",
         final_diagnostic.at("research").at("civilizations").size()},
        {"diplomaticContacts",
         final_diagnostic.at("diplomacy").at("contacts").size()},
        {"diplomaticRelationships",
         final_diagnostic.at("diplomacy").at("relationships").size()},
        {"combatIntelligenceObservations",
         final_diagnostic.at("combatIntelligence").size()},
        {"fleets", fleet_count}}},
      {"legacyResearchDisabled", true},
      {"stateAdvancedBeyondSeed", advanced},
      {"repeatFinalStatesDeterministic", true},
      {"finalStateHash", final_hash},
      {"assetPath",
       (asset_root / "Data/astronomy/hyg-nearby-500-v1.json").string()},
      {"researchAssetPath", research_root.string()},
      {"diagnosticOutput", options.output.string()}};
  std::cout << report.dump() << '\n';
  return 0;
}
