#include <stellar/core/adaptive_research_strategic_runtime.hpp>

#include <nlohmann/json.hpp>

#include <cmath>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <string>
#include <type_traits>

namespace {
using json = nlohmann::ordered_json;
using namespace stellar::core;

void require(bool condition, std::string_view message) {
  if (!condition)
    throw std::runtime_error(std::string(message));
}

bool equal(const json &left, const json &right) {
  if (left.is_number() && right.is_number()) {
    const auto a = left.get<double>();
    const auto b = right.get<double>();
    return a == b || std::abs(a - b) <= 1e-10;
  }
  if (left.type() != right.type())
    return false;
  if (left.is_array()) {
    if (left.size() != right.size())
      return false;
    for (std::size_t i = 0; i < left.size(); ++i)
      if (!equal(left[i], right[i]))
        return false;
    return true;
  }
  if (left.is_object()) {
    if (left.size() != right.size())
      return false;
    for (const auto &[key, value] : left.items()) {
      const auto found = right.find(key);
      if (found == right.end() || !equal(value, *found))
        return false;
    }
    return true;
  }
  return left == right;
}

void require_equal(const json &actual, const json &expected,
                   std::string_view label) {
  if (!equal(actual, expected))
    throw std::runtime_error(std::string(label) + " differs.\nactual=" +
                             actual.dump() + "\nexpected=" + expected.dump());
}

json core_json(const AdaptiveResearchCivilizationState &state) {
  json nodes = json::array();
  for (const auto &value : state.node_states())
    nodes.push_back({{"NodeId", value.node_id},
                     {"Maturity", static_cast<int>(value.maturity)},
                     {"Resolution", value.resolution},
                     {"StageResearchPoints", value.stage_research_points},
                     {"TotalResearchPoints", value.total_research_points},
                     {"Revision", value.revision}});
  json pressures = json::array();
  for (const auto &value : state.pressures())
    pressures.push_back({{"Id", value.pressure_id}, {"Value", value.value}});
  return {{"CivilizationId", state.civilization_id()},
          {"Revision", state.revision()},
          {"MaterializedViewRevision", state.materialized_view_revision()},
          {"Nodes", std::move(nodes)},
          {"Pressures", std::move(pressures)}};
}

json pressure_json(const AdaptiveResearchPressureState &state) {
  json signals = json::array();
  for (const auto &value : state.metric_signals())
    signals.push_back({{"Id", value.signal_id}, {"Value", value.value}});
  return {{"Revision", state.revision()},
          {"MetricSignals", std::move(signals)},
          {"ActivePressureIds", state.active_pressure_ids()}};
}

json agenda_json(const AdaptiveResearchAgendaState &state) {
  json problems = json::array();
  for (const auto &value : state.problem_priorities())
    problems.push_back({{"Id", value.key}, {"Priority", value.priority_id}});
  return {{"Revision", state.revision()}, {"Problems", std::move(problems)}};
}

json assessment_json(const ForeignTechnologyAssessmentRuntimeState &value) {
  return {{"ForeignTechnologyReference", value.foreign_technology_reference},
          {"SourceLineageReference", value.source_lineage_reference},
          {"Understanding", static_cast<int>(value.understanding)},
          {"Adaptation", static_cast<int>(value.adaptation)},
          {"KnownConstraintIds", value.known_constraint_ids},
          {"Confidence", value.confidence},
          {"Revision", value.revision}};
}

json foreign_json(const AdaptiveResearchForeignTechnologyState &state) {
  json values = json::array();
  for (const auto &value : state.assessments())
    values.push_back(assessment_json(value));
  return {{"Revision", state.revision()}, {"Assessments", std::move(values)}};
}

json discovery_json(const ForeignResearchDiscoveryResult &result) {
  json events = json::array();
  for (const auto &event : result.awareness_events)
    events.push_back(
        {{"ForeignTechnologyReference", event.foreign_technology_reference},
         {"NodeId", event.node_id},
         {"PreviousState", event.previous_state
                               ? json(static_cast<int>(*event.previous_state))
                               : json(nullptr)},
         {"NewState", static_cast<int>(event.new_state)},
         {"Reason", event.reason}});
  return {{"Assessment", assessment_json(result.assessment)},
          {"Events", std::move(events)}};
}

json plan_json(const PlannedResearchOutcome &value) {
  return {{"NodeId", value.node_id},
          {"CheckpointId", value.checkpoint_id},
          {"AttemptIndex", value.attempt_index},
          {"Profile", static_cast<int>(value.profile)},
          {"Outcome", static_cast<int>(value.outcome)},
          {"DeterministicRoll", value.deterministic_roll},
          {"PlannedSideDiscoveryNodeId", value.planned_side_discovery_node_id},
          {"Explanation", value.explanation}};
}

json state_bundle(const AdaptiveResearchStrategicRuntime &runtime,
                  const AdaptiveResearchCivilizationState &state) {
  return {{"Core", core_json(state)},
          {"Pressure", pressure_json(runtime.pressure().get_support_state(state))},
          {"Agenda", agenda_json(runtime.agenda().state(state))},
          {"Foreign", foreign_json(runtime.foreign_technology().state(state))},
          {"OutcomeRevision", runtime.outcomes().state(state).revision()}};
}
} // namespace

int main(int argc, char **argv) {
  try {
    if (argc != 3)
      throw std::runtime_error(
          "usage: adaptive_research_strategic_runtime_tests <fixture> <research-data>");
    std::ifstream stream(argv[1]);
    if (!stream)
      throw std::runtime_error("Could not open fixture.");
    json expected;
    stream >> expected;
    const std::filesystem::path root(argv[2]);

    auto runtime = load_adaptive_research_strategic_runtime(root);
    auto state = runtime.authority().create_civilization_state(
        "fixture:strategic-owner");
    json actual;
    actual["Generator"] = "actual C# AdaptiveResearchStrategicRuntime";
    actual["Catalogs"] = {
        {"CatalogId", runtime.authority().catalog().metadata().catalog_id},
        {"PressureRules", runtime.pressure_catalog().rules().size()},
        {"AgendaPriorities", runtime.agenda_catalog().priorities().size()},
        {"ForeignComponents",
         runtime.foreign_technology_catalog().components().size()},
        {"DiscoveryRules",
         runtime.foreign_discovery_catalog().method_rules().size()},
        {"OutcomeSideIndexes",
         runtime.outcome_catalog().side_discovery_candidates().size()}};
    actual["StableRepeatedGetters"] =
        &runtime.authority() == &runtime.authority() &&
        &runtime.pressure() == &runtime.pressure() &&
        &runtime.agenda() == &runtime.agenda() &&
        &runtime.foreign_technology() == &runtime.foreign_technology() &&
        &runtime.foreign_discovery() == &runtime.foreign_discovery() &&
        &runtime.outcomes() == &runtime.outcomes();
    actual["Initial"] = state_bundle(runtime, state);

    runtime.pressure().report_metric_signal(
        state, "unresolved_alien_signal_relevance", 0.6);
    runtime.agenda().set_problem_priority(state, "alien_signal", "critical");
    const std::string constraints[] = {"scientific_gap"};
    const auto discovery = runtime.foreign_discovery().observe(
        state, "foreign:facade", "lineage:facade", 0.75, 2120.0,
        constraints);
    const auto plan = runtime.outcomes().plan_outcome(
        state, "xenolinguistics", "seed:facade", "checkpoint:facade");
    actual["Discovery"] = discovery_json(discovery);
    actual["Plan"] = plan_json(plan);
    actual["Final"] = state_bundle(runtime, state);
    require_equal(actual, expected, "Actual-source composition fixture");

    const auto *authority_address = &runtime.authority();
    const auto *pressure_catalog_address = &runtime.pressure_catalog();
    const auto *pressure_address = &runtime.pressure();
    const auto *agenda_catalog_address = &runtime.agenda_catalog();
    const auto *agenda_address = &runtime.agenda();
    const auto *foreign_catalog_address =
        &runtime.foreign_technology_catalog();
    const auto *foreign_address = &runtime.foreign_technology();
    const auto *discovery_catalog_address =
        &runtime.foreign_discovery_catalog();
    const auto *discovery_address = &runtime.foreign_discovery();
    const auto *outcome_catalog_address = &runtime.outcome_catalog();
    const auto *outcomes_address = &runtime.outcomes();
    const auto *pressure_support = &runtime.pressure().get_support_state(state);
    const auto *agenda_support = &runtime.agenda().state(state);
    const auto *foreign_support = &runtime.foreign_technology().state(state);
    const auto *outcome_support = &runtime.outcomes().state(state);

    auto moved = std::move(runtime);
    require(&moved.authority() == authority_address &&
                &moved.pressure_catalog() == pressure_catalog_address &&
                &moved.pressure() == pressure_address &&
                &moved.agenda_catalog() == agenda_catalog_address &&
                &moved.agenda() == agenda_address &&
                &moved.foreign_technology_catalog() ==
                    foreign_catalog_address &&
                &moved.foreign_technology() == foreign_address &&
                &moved.foreign_discovery_catalog() ==
                    discovery_catalog_address &&
                &moved.foreign_discovery() == discovery_address &&
                &moved.outcome_catalog() == outcome_catalog_address &&
                &moved.outcomes() == outcomes_address,
            "Move construction changed owned member addresses.");
    require(&moved.pressure().get_support_state(state) == pressure_support &&
                &moved.agenda().state(state) == agenda_support &&
                &moved.foreign_technology().state(state) == foreign_support &&
                &moved.outcomes().state(state) == outcome_support,
            "Move construction lost support-state identity.");

    auto target = load_adaptive_research_strategic_runtime(root);
    auto displaced_state = target.authority().create_civilization_state(
        "fixture:displaced-owner");
    target.pressure().report_metric_signal(
        displaced_state, "unresolved_alien_signal_relevance", 0.25);
    require(target.pressure().get_support_state(displaced_state).revision() > 0,
            "Displaced owner probe did not establish support.");
    target = std::move(moved);
    require(&target.authority() == authority_address &&
                &target.pressure_catalog() == pressure_catalog_address &&
                &target.pressure() == pressure_address &&
                &target.agenda_catalog() == agenda_catalog_address &&
                &target.agenda() == agenda_address &&
                &target.foreign_technology_catalog() ==
                    foreign_catalog_address &&
                &target.foreign_technology() == foreign_address &&
                &target.foreign_discovery_catalog() ==
                    discovery_catalog_address &&
                &target.foreign_discovery() == discovery_address &&
                &target.outcome_catalog() == outcome_catalog_address &&
                &target.outcomes() == outcomes_address &&
                &target.pressure().get_support_state(state) == pressure_support,
            "Move assignment did not transfer the incoming stable graph.");
    require(target.pressure().get_support_state(displaced_state).revision() == 0,
            "Move assignment retained displaced-owner support.");

    const auto original_pressure_revision = pressure_support->revision();
    auto copy = state;
    require(target.pressure().get_support_state(copy).revision() == 0 &&
                target.agenda().state(copy).revision() == 0 &&
                target.foreign_technology().state(copy).revision() == 0 &&
                target.outcomes().state(copy).revision() == 0,
            "Civilization copy reused runtime support identity.");
    target.pressure().report_metric_signal(
        copy, "unresolved_alien_signal_relevance", 0.2);
    require(target.pressure().get_support_state(state).revision() ==
                original_pressure_revision,
            "Copied-state support mutation affected the original.");

    static_assert(!std::is_copy_constructible_v<
                  AdaptiveResearchStrategicRuntime>);
    static_assert(!std::is_copy_assignable_v<
                  AdaptiveResearchStrategicRuntime>);
    std::cout << "adaptive_research_strategic_runtime_tests: actual-source "
                 "composition and native ownership probes passed\n";
    return 0;
  } catch (const std::exception &error) {
    std::cerr << "adaptive_research_strategic_runtime_tests failed: "
              << typeid(error).name() << ": " << error.what()
              << "\nCWD: " << std::filesystem::current_path().string()
              << "\nFixture: " << (argc > 1 ? argv[1] : "<missing>")
              << "\nResearch root: " << (argc > 2 ? argv[2] : "<missing>")
              << '\n';
    return 1;
  }
}
