#include "adaptive_campaign_host.hpp"

#include <stellar/build_version.hpp>
#include <stellar/core/adaptive_research_campaign.hpp>
#include <stellar/core/detail/adaptive_research_outcome_snapshot_json.hpp>
#include <stellar/core/detail/adaptive_research_sha256.hpp>
#include <stellar/core/diplomacy_state.hpp>
#include <stellar/core/integrated_adaptive_campaign.hpp>
#include <stellar/core/species_environment.hpp>
#include <stellar/engine/runtime_paths.hpp>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <fstream>
#include <iomanip>
#include <iostream>
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
  (void)species_environment_profile(options.player_species);
  const auto asset_root = std::filesystem::absolute(
      options.asset_root.empty() ? stellar::engine::executable_directory()
                                 : options.asset_root);
  const auto research_root = std::filesystem::absolute(
      asset_root / "Data/research/v1");
  std::string retained_state;
  Json final_diagnostic;
  bool advanced = true;
  std::uint64_t sensor_contacts = 0, adaptive_events = 0,
                diplomacy_events = 0, industry_allocations = 0,
                construction_events = 0, shipbuilding_events = 0,
                exploration_events = 0, combat_events = 0,
                colonization_events = 0;
  double initialization_total_ms = 0.0, step_total_ms = 0.0;
  std::vector<double> step_times;

  for (int repeat = 0; repeat < options.repeats; ++repeat) {
    const auto initialization_started = std::chrono::steady_clock::now();
    auto runtime = IntegratedAdaptiveCampaignRuntime::create_fresh(
        load_runtime(research_root),
        seed_fresh_campaign(options.seed, stellar_catalog, options.systems,
                            options.pre_warp_civilizations,
                            options.ancient_civilizations,
                            options.player_species));
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
    }
    final_diagnostic = adaptive_diagnostic(runtime, campaign_diagnostic);
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
         final_diagnostic.at("combatIntelligence").size()}}},
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
