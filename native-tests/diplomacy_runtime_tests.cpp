#include <stellar/core/combat_state.hpp>
#include <stellar/core/diplomacy_runtime.hpp>
#include <stellar/core/galaxy_catalog.hpp>

#include "diplomacy_test_json.hpp"

#include <array>
#include <filesystem>
#include <iostream>
#include <string>
#include <type_traits>

namespace {
ExplorationEvent exploration(ExplorationEventType type, int observer,
                             std::optional<int> target = 2) {
  return {type, observer, 80, 7, "explore", target, std::nullopt};
}
CombatEvent combat(CombatEventType type, int actor = 1,
                   std::optional<int> target = 2,
                   std::optional<int> system = 7) {
  return {type, system, actor, 10, target, 20, 0, 0, 0, "combat", 0};
}
ExplorationEvent decode_exploration(const Json &value) {
  return {static_cast<ExplorationEventType>(value.at("Type").get<int>()),
          value.at("CivilizationId").get<int>(),
          value.at("FleetId").get<int>(),
          value.at("SystemId").get<int>(),
          value.at("Message").get<std::string>(),
          opt<int>(value, "TargetCivilizationId"),
          opt<int>(value, "PlanetaryBodyId")};
}
CombatEvent decode_combat(const Json &value) {
  return {static_cast<CombatEventType>(value.at("Type").get<int>()),
          opt<int>(value, "SystemId"),
          value.at("ActorCivilizationId").get<int>(),
          value.at("ActorFleetId").get<int>(),
          opt<int>(value, "TargetCivilizationId"),
          opt<int>(value, "TargetFleetId"),
          number(value.at("ShieldDamage")),
          number(value.at("ArmorDamage")),
          number(value.at("HullDamage")),
          value.at("Message").get<std::string>(),
          number(value.at("EmbarkedPopulationCasualtiesMillions"))};
}
FirstContactOpportunity contact(int observer, int target, std::int64_t tick,
                                std::string id = {}) {
  return {observer,
          id.empty() ? "contact-" + std::to_string(observer) + "-" +
                           std::to_string(target)
                     : std::move(id),
          target,
          tick,
          7,
          ContactAwareness::communication_available,
          ContactCondition::active,
          true,
          1};
}
void identified(DiplomacyState &state, int observer = 2, int target = 1) {
  DiplomacySimulation simulation(state);
  (void)simulation.process_contact_opportunity(contact(observer, target, 1));
}
Json jmaintenance(const DiplomacyCampaignMaintenanceResult &v) {
  return {{"Ran", v.ran},
          {"ReviewTick", v.review_tick},
          {"ContactAging",
           {{"ReviewedContacts", v.contact_aging.reviewed_contacts},
            {"NewlyStaleContacts", v.contact_aging.newly_stale_contacts}}},
          {"ProposalLifecycle",
           {{"PendingProposalsReviewed",
             v.proposal_lifecycle.pending_proposals_reviewed},
            {"NewlyExpiredProposals",
             v.proposal_lifecycle.newly_expired_proposals}}}};
}
Json jstep(const DiplomacyCampaignRuntimeStepResult &v) {
  return {{"Tick", v.tick},
          {"FirstContactEventsProcessed", v.first_contact_events_processed},
          {"CombatIncidentsProcessed", v.combat_incidents_processed},
          {"Maintenance", jmaintenance(v.maintenance)},
          {"MaintenanceTransitions", v.maintenance_transitions()},
          {"ProcessedDiplomacyEvents", v.processed_diplomacy_events()}};
}
Json jknowledge(const StrategicKnowledgeSnapshot &v) {
  Json civilizations = Json::object();
  for (const auto &entry : v.civilizations) {
    const auto &x = entry.civilization;
    civilizations[std::to_string(entry.key)] = {
        {"CivilizationId", x.civilization_id},
        {"Trust", x.trust},
        {"EstimatedMilitaryLow", x.estimated_military_low},
        {"EstimatedMilitaryHigh", x.estimated_military_high},
        {"EstimateConfidence", x.estimate_confidence},
        {"LastMilitaryObservationTick", x.last_military_observation_tick},
        {"HasSharedBorder", x.has_shared_border},
        {"KnownTradeDependence", x.known_trade_dependence},
        {"KnownWarExhaustion", x.known_war_exhaustion},
        {"KnownToBeAtWar", x.known_to_be_at_war},
        {"HasDefenseTreatyWithObserver", x.has_defense_treaty_with_observer},
        {"HasMilitaryEstimate", x.has_military_estimate},
        {"EstimatedMilitaryMidpoint",
         (x.estimated_military_low + x.estimated_military_high) * 0.5}};
  }
  return {{"ObservedAtTick", v.observed_at_tick},
          {"Civilizations", civilizations}};
}
std::optional<Error> invoke_runtime(auto &&call) {
  try {
    call();
  } catch (const DiplomacyArgumentRangeError &e) {
    return Error{"ArgumentOutOfRangeException", e.what()};
  } catch (const DiplomacyOperationError &e) {
    return Error{"InvalidOperationException", e.what()};
  }
  return std::nullopt;
}
} // namespace

int main(int argc, char **argv) {
  try {
    if (argc != 3)
      throw std::runtime_error(
          "Usage: diplomacy_runtime_tests <fixture-path> <source-root>");
    const auto fixture_path = std::filesystem::absolute(argv[1]);
    const auto root = std::filesystem::absolute(argv[2]);
    const auto fixture_bytes = read(fixture_path);
    const auto digest = detail::adaptive_research_sha256(std::span(
        reinterpret_cast<const std::uint8_t *>(fixture_bytes.data()), fixture_bytes.size()));
    require(hex(digest) == "11DC3AE99A3D1388E1A4FE97EBB072A86B2AA68AA842C8ACEDE91F460AE68C55",
            "retained fixture fingerprint mismatch");
    const Json fixture = Json::parse(fixture_bytes);
    require(fixture.at("Schema") == "stellar-diplomacy-runtime-oracle-v1" && fixture.at("RowCount") == 22 &&
                fixture.at("Rows").size() == 22, "retained fixture schema/count mismatch");
    const auto verify_sources = [&] {
    for (const auto &source : fixture.at("SourceFiles")) {
      const auto bytes = read(root / source.at("Path").get<std::string>());
      const auto digest = detail::adaptive_research_sha256(std::span(
          reinterpret_cast<const std::uint8_t *>(bytes.data()), bytes.size()));
      require(hex(digest) == source.at("Sha256").get<std::string>(),
              "source fingerprint mismatch");
    }
    };
    verify_sources();
    int checked = 0;
    for (const auto &row : fixture.at("Rows")) {
      const std::string name = row.at("Name").get<std::string>();
      const Json arguments = row.at("Arguments");
      require(arguments == row.at("InputBefore") &&
                  arguments == row.at("InputAfter"),
              name + " retained input mutated");
      const std::string case_id = arguments.at("Case").get<std::string>();
      const Json &payload = arguments.at("Payload");
      DiplomacyState state =
          DiplomacyState::restore(snapshot(row.at("Before")));
      require(jsnapshot(state.snapshot()) == row.at("Before"),
              name + " native state before mismatch");
      Json result = nullptr;
      std::optional<Error> actual_error;

      if (row.at("Operation") == "Exploration") {
        DiplomacySimulation simulation(state);
        ExplorationDiplomacyBridge bridge(simulation);
        std::vector<ExplorationEvent> events;
        for (const auto &event : payload.at("Events"))
          events.push_back(decode_exploration(event));
        const auto tick = payload.at("Tick").get<std::int64_t>();
        int value{};
        actual_error =
            invoke_runtime([&] { value = bridge.process(events, tick); });
        if (!actual_error)
          result = value;
      } else if (row.at("Operation") == "Combat") {
        CombatDiplomacyBridge bridge(state);
        std::vector<CombatEvent> events;
        for (const auto &event : payload.at("Events"))
          events.push_back(decode_combat(event));
        const auto tick = payload.at("Tick").get<std::int64_t>();
        int value{};
        actual_error =
            invoke_runtime([&] { value = bridge.process(events, tick); });
        if (!actual_error)
          result = value;
      } else if (row.at("Operation") == "Hostility") {
        DiplomacyCombatHostilityView view(state);
        bool other{}, self{};
        const int first = payload.at("First").get<int>();
        const int second = payload.at("Second").get<int>();
        const int same = payload.at("Self").get<int>();
        actual_error = invoke_runtime([&] {
          other = view.are_hostile(first, second);
          self = view.are_hostile(same, same);
        });
        if (!actual_error)
          result = {{"Other", other}, {"Self", self}};
      } else if (row.at("Operation") == "Knowledge") {
        DiplomacyStrategicKnowledgeProvider provider(state);
        StrategicKnowledgeSnapshot value;
        const int observer = payload.at("Observer").get<int>();
        const auto tick = payload.at("Tick").get<std::int64_t>();
        actual_error =
            invoke_runtime([&] { value = provider.build(observer, tick); });
        if (!actual_error)
          result = jknowledge(value);
      } else if (case_id == "runtime-reset-immediate") {
        DiplomacyCampaignRuntimeCoordinator runtime(state);
        const double reset_days = payload.at("ResetDays").get<double>();
        const bool reset_immediate = payload.at("ResetImmediate").get<bool>();
        actual_error = invoke_runtime([&] {
          runtime.reset(reset_days, reset_immediate);
        });
        if (!actual_error)
          result = {{"LastProcessedTick", runtime.last_processed_tick()},
                    {"NextMaintenanceReviewTick",
                     runtime.next_maintenance_review_tick()}};
      } else if (case_id == "runtime-success-equal-and-backward") {
        const auto &policy = payload.at("Policy");
        DiplomacyCampaignRuntimeCoordinator runtime(
            state, DiplomacyCampaignMaintenancePolicy{
                       policy.at("ReviewIntervalTicks").get<std::int64_t>(),
                       policy.at("ContactStaleAfterTicks").get<std::int64_t>(),
                       policy.at("ProposalLifetimeTicks").get<std::int64_t>()});
        DiplomacyCampaignRuntimeStepResult first, equal;
        std::optional<Error> backward;
        const auto &steps = payload.at("Steps");
        auto events0 = std::vector<ExplorationEvent>{};
        for (const auto &e : steps[0].at("ExplorationEvents"))
          events0.push_back(decode_exploration(e));
        const double reset_days = payload.at("ResetDays").get<double>();
        const bool reset_immediate = payload.at("ResetImmediate").get<bool>();
        const double first_day = steps[0].at("Days").get<double>();
        const double equal_day = steps[1].at("Days").get<double>();
        const double backward_day = steps[2].at("Days").get<double>();
        require(steps[0].at("CombatEvents").empty() &&
                    steps[1].at("ExplorationEvents").empty() && steps[1].at("CombatEvents").empty() &&
                    steps[2].at("ExplorationEvents").empty() && steps[2].at("CombatEvents").empty(),
                name + " unsupported event sequence");
        actual_error = invoke_runtime([&] {
          runtime.reset(reset_days, reset_immediate);
          first = runtime.process(events0, {}, first_day);
          equal = runtime.process({}, {}, equal_day);
          backward = invoke_runtime([&] {
            (void)runtime.process({}, {}, backward_day);
          });
        });
        require(actual_error || backward.has_value(), name + " missing backward rejection");
        if (!actual_error)
          result = {
              {"First", jstep(first)},
              {"Equal", jstep(equal)},
              {"Backward",
               {{"Type", backward->type}, {"Message", backward->message}}},
              {"LastProcessedTick", runtime.last_processed_tick()},
              {"NextMaintenanceReviewTick",
               runtime.next_maintenance_review_tick()}};
      } else if (case_id == "runtime-partial-exploration") {
        const auto &policy = payload.at("Policy");
        DiplomacyCampaignRuntimeCoordinator runtime(
            state, DiplomacyCampaignMaintenancePolicy{
                       policy.at("ReviewIntervalTicks").get<std::int64_t>(),
                       policy.at("ContactStaleAfterTicks").get<std::int64_t>(),
                       policy.at("ProposalLifetimeTicks").get<std::int64_t>()});
        std::optional<Error> nested;
        std::vector<ExplorationEvent> events;
        std::vector<CombatEvent> combats;
        for (const auto &e : payload.at("Steps")[0].at("ExplorationEvents"))
          events.push_back(decode_exploration(e));
        for (const auto &e : payload.at("Steps")[0].at("CombatEvents"))
          combats.push_back(decode_combat(e));
        const double reset_days = payload.at("ResetDays").get<double>();
        const bool reset_immediate = payload.at("ResetImmediate").get<bool>();
        const double day = payload.at("Steps")[0].at("Days").get<double>();
        actual_error = invoke_runtime([&] {
          runtime.reset(reset_days, reset_immediate);
          nested = invoke_runtime([&] { (void)runtime.process(events, combats, day); });
        });
        require(actual_error || nested.has_value(), name + " missing partial failure");
        if (!actual_error)
          result = {
              {"Error", {{"Type", nested->type}, {"Message", nested->message}}},
              {"LastProcessedTick", runtime.last_processed_tick()},
              {"NextMaintenanceReviewTick",
               runtime.next_maintenance_review_tick()}};
      } else if (row.at("Operation") == "Preview") {
        require(payload.at("Systems").size() == 1 && payload.at("Fleets").size() == 2 &&
                    payload.at("Sequence") == Json::array({"PreviewPeace", "SetHostile", "PreviewHostile", "IssueHostile"}),
                name + " unsupported command sequence");
        StellarSystem system;
        system.id = payload.at("Systems")[0].at("Id").get<int>();
        system.name = payload.at("Systems")[0].at("Name").get<std::string>();
        std::array<FleetState, 2> fleets;
        for (std::size_t index = 0; index < fleets.size(); ++index) {
          const auto &input = payload.at("Fleets")[index];
          auto &fleet = fleets[index];
          fleet.id = input.at("Id").get<int>();
          fleet.civilization_id = input.at("CivilizationId").get<int>();
          fleet.name = input.at("Name").get<std::string>();
          fleet.current_system_id = input.at("SystemId").get<int>();
          fleet.role = FleetRole::Military;
          fleet.combat = create_initial_fleet_combat_state(std::nullopt, FleetRole::Military);
        }
        std::array systems{system};
        const MilitaryOrder order{
            static_cast<MilitaryOrderType>(payload.at("Order").at("Type").get<int>()),
            opt<int>(payload.at("Order"), "TargetFleetId"),
            opt<int>(payload.at("Order"), "DefendSystemId")};
        const int observer = fleets[0].civilization_id;
        const int target = fleets[1].civilization_id;
        const int fleet_id = fleets[0].id;
        DiplomacyCampaignRuntimeCoordinator runtime(state);
        auto commands = runtime.create_combat_command_runtime();
        DiplomacySimulation simulation(state);
        CombatOrderPreview peace, hostile;
        CombatOrderResult issued;
        actual_error = invoke_runtime([&] {
          peace = commands.preview_order({systems, fleets}, observer, fleet_id, order);
          simulation.set_hostile(observer, target, 3, "hostile");
          hostile = commands.preview_order({systems, fleets}, observer, fleet_id, order);
          issued = commands.issue_order({systems, fleets}, observer, fleet_id, order);
        });
        auto preview = [](const CombatOrderPreview &v) {
          return Json{{"CivilizationId", v.civilization_id},
                      {"FleetId", v.fleet_id},
                      {"Order",
                       {{"Type", static_cast<int>(v.order.type)},
                        {"TargetFleetId", v.order.target_fleet_id
                                              ? Json(*v.order.target_fleet_id)
                                              : Json(nullptr)},
                        {"DefendSystemId", nullptr}}},
                      {"Accepted", v.accepted},
                      {"Message", v.message}};
        };
        if (!actual_error)
          result = {
              {"Peace", preview(peace)},
              {"Hostile", preview(hostile)},
              {"Issued",
               {{"Accepted", issued.accepted}, {"Message", issued.message}}},
              {"FleetOrder", static_cast<int>(fleets[0].combat->order)},
              {"FleetTarget", fleets[0].combat->target_fleet_id
                                  ? Json(*fleets[0].combat->target_fleet_id)
                                  : Json(nullptr)}};
      } else {
        throw std::runtime_error(name + " unsupported fixture operation");
      }
      error(row.at("Error"), actual_error, name);
      require(result == row.at("Result"), name + " result mismatch\n" +
                                              result.dump() + "\n" +
                                              row.at("Result").dump());
      require(jsnapshot(state.snapshot()) == row.at("After"),
              name + " state mismatch");
      ++checked;
    }
    static_assert(!std::is_constructible_v<DiplomacyCampaignRuntimeCoordinator,
                                           DiplomacyState &&>);
    {
      DiplomacyState state;
      identified(state, 1, 2);
      identified(state, 2, 1);
      DiplomacyCampaignRuntimeCoordinator original(state);
      auto callback = original.hostility_view().combat_hostility_view();
      auto detached = original.build_view(1);
      const auto detached_projection = jview(detached);
      auto *commands_address = &original.commands();
      DiplomacyCampaignRuntimeCoordinator moved(std::move(original));
      require(&moved.commands() == commands_address && &moved.state() == &state,
              "runtime move changed stable owned service/state borrows");
      DiplomacySimulation simulation(state);
      simulation.set_hostile(1, 2, 10, "move proof");
      require(callback(1, 2) && moved.hostility_view().are_hostile(1, 2),
              "state-capturing callback or moved runtime became stale");
      require(jview(detached) == detached_projection,
              "detached observer view changed after later state mutation");
    }
    verify_sources();
    require(checked == 22, "native row accounting mismatch");
    std::cout << "diplomacy runtime parity: " << checked << " rows passed\n";
    return 0;
  } catch (const std::exception &e) {
    std::cerr << "diplomacy runtime replay failed [" << typeid(e).name()
              << "]: " << e.what()
              << "\ncwd=" << std::filesystem::current_path() << "\nfixture="
              << (argc > 1 ? std::filesystem::absolute(argv[1]).string()
                           : "<missing>")
              << "\nsource-root="
              << (argc > 2 ? std::filesystem::absolute(argv[2]).string()
                           : "<missing>")
              << '\n';
    return 1;
  }
}
