#include <stellar/core/adaptive_research_funding.hpp>

#include <stellar/core/detail/adaptive_research_campaign_state_access.hpp>
#include <stellar/core/detail/adaptive_research_sha256.hpp>
#include <stellar/core/detail/adaptive_research_state_writer.hpp>

#include <nlohmann/json.hpp>

#include <cmath>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <limits>
#include <optional>
#include <span>
#include <stdexcept>
#include <string>

using Json = nlohmann::ordered_json;
using namespace stellar::core;

namespace {

void require(bool condition, std::string message) {
  if (!condition) throw std::runtime_error(std::move(message));
}

std::string read(const std::filesystem::path &path) {
  std::ifstream input(path, std::ios::binary);
  if (!input)
    throw std::runtime_error("Could not open '" + path.string() + "'.");
  return {std::istreambuf_iterator<char>(input), {}};
}

std::string hex(std::span<const std::uint8_t> value) {
  constexpr std::string_view digits = "0123456789ABCDEF";
  std::string result;
  for (const auto byte : value) {
    result.push_back(digits[byte >> 4]);
    result.push_back(digits[byte & 15]);
  }
  return result;
}

std::string fingerprint(const std::filesystem::path &root) {
  std::vector<std::filesystem::path> files;
  for (const auto &entry : std::filesystem::directory_iterator(root))
    if (entry.is_regular_file() && entry.path().extension() == ".json")
      files.push_back(entry.path());
  std::ranges::sort(files, [](const auto &left, const auto &right) {
    return left.filename().string() < right.filename().string();
  });
  std::string combined;
  for (const auto &path : files) {
    combined += path.filename().string();
    combined += read(path);
  }
  return hex(detail::adaptive_research_sha256(
      {reinterpret_cast<const std::uint8_t *>(combined.data()), combined.size()}));
}

double number(const Json &value) {
  if (value.is_number()) return value.get<double>();
  const auto text = value.get<std::string>();
  if (text == "NaN") return std::numeric_limits<double>::quiet_NaN();
  if (text == "Infinity") return std::numeric_limits<double>::infinity();
  if (text == "-Infinity") return -std::numeric_limits<double>::infinity();
  throw std::runtime_error("Unknown retained number '" + text + "'.");
}

bool same(double left, double right) {
  return (std::isnan(left) && std::isnan(right)) || left == right;
}

struct Error {
  std::string type;
  std::string message;
};

template <class Call>
std::optional<Error> invoke(Call &&call) {
  try {
    std::forward<Call>(call)();
  } catch (const AdaptiveResearchFundingArgumentRangeError &error) {
    return Error{"ArgumentOutOfRangeException", error.what()};
  } catch (const AdaptiveResearchFundingPolicyError &error) {
    return Error{"InvalidOperationException", error.what()};
  } catch (const AdaptiveResearchFundingSequenceError &error) {
    return Error{"InvalidOperationException", error.what()};
  } catch (const std::out_of_range &error) {
    return Error{"ArgumentOutOfRangeException", error.what()};
  }
  return std::nullopt;
}

void compare_error(const Json &expected, const std::optional<Error> &actual,
                   std::string_view name) {
  if (expected.is_null()) {
    require(!actual, std::string(name) + " unexpectedly failed.");
    return;
  }
  require(actual.has_value(), std::string(name) + " unexpectedly succeeded.");
  require(actual->type == expected.at("Type").get<std::string>(),
          std::string(name) + " exception type differed: " + actual->type);
  require(actual->message == expected.at("Message").get<std::string>(),
          std::string(name) + " exception message differed: " + actual->message);
}

void compare_quote(const Json &expected,
                   const AdaptiveResearchFundingQuote &actual,
                   std::string_view name) {
  require(same(actual.assigned_effective_labs,
               number(expected.at("AssignedEffectiveLabs"))) &&
              same(actual.authorization_credits,
                   number(expected.at("AuthorizationCredits"))) &&
              same(actual.milestone_commitment_credits,
                   number(expected.at("MilestoneCommitmentCredits"))) &&
              same(actual.operating_credits_per_day,
                   number(expected.at("OperatingCreditsPerDay"))) &&
              same(actual.estimated_total_operating_credits,
                   number(expected.at("EstimatedTotalOperatingCredits"))) &&
              same(actual.estimated_total_credits,
                   number(expected.at("EstimatedTotalCredits"))) &&
              same(actual.estimated_years_at_full_funding,
                   number(expected.at("EstimatedYearsAtFullFunding"))) &&
              actual.complexity == expected.at("Complexity").get<std::string>(),
          std::string(name) + " quote differed.");
}

Civilization civilization() {
  Civilization result;
  result.id = 1;
  result.name = "Civilization 1";
  result.archetype = CivilizationArchetype::Scientific;
  result.is_player = true;
  result.development_stage = CivilizationDevelopmentStage::WarpCapable;
  result.species_id = "terran_baseline";
  return result;
}

struct Setup {
  std::vector<Civilization> civilizations;
  std::vector<CivilizationEconomy> economies;
  AdaptiveResearchCampaignState campaign;
  std::string node_id;
  double labs{};
};

Setup setup(const AdaptiveResearchStrategicRuntime &runtime,
            double credits = 500) {
  std::vector<Civilization> civilizations{civilization()};
  std::vector<CivilizationEconomy> economies{{1, credits}};
  AdaptiveResearchCampaignFactory factory(runtime);
  auto campaign = factory.create(civilizations);
  const auto &state = campaign.get_civilization(1);
  const auto view = runtime.authority().build_view(state);
  const auto found = std::ranges::find_if(view.visible_nodes, [](const auto &node) {
    return node.state == ResearchMaturity::investigable &&
           node.blockers.empty() && node.minimum_labs.has_value();
  });
  require(found != view.visible_nodes.end(), "No canonical start candidate.");
  return {std::move(civilizations), std::move(economies), std::move(campaign),
          found->node_id, static_cast<double>(*found->minimum_labs)};
}

Json project_json(const ResearchProjectRuntimeState &value) {
  return {{"NodeId", value.node_id},
          {"Stage", static_cast<int>(value.stage)},
          {"TargetApplicabilityContextId", value.target_applicability_context_id
                                                 ? Json(*value.target_applicability_context_id)
                                                 : Json(nullptr)},
          {"AssignedEffectiveLabs", value.assigned_effective_labs},
          {"ReadinessEfficiency", value.readiness_efficiency},
          {"Paused", value.paused},
          {"PauseReason", value.pause_reason ? Json(*value.pause_reason)
                                               : Json(nullptr)},
          {"StageResearchPoints", value.stage_research_points},
          {"TotalResearchPoints", value.total_research_points},
          {"Revision", value.revision}};
}

Json funding_json(std::span<const AdaptiveResearchProjectFundingState> values) {
  Json result = Json::array();
  for (const auto &value : values)
    result.push_back({{"NodeId", value.node_id},
                      {"ReservedMilestoneCredits", value.reserved_milestone_credits},
                      {"ConsumedMilestoneCredits", value.consumed_milestone_credits},
                      {"AuthorizationCredits", value.authorization_credits}});
  return result;
}

void compare_campaign(const Json &expected, const Setup &value,
                      int civilization_id, std::string_view node_id,
                      std::string_view name) {
  const auto economy = std::ranges::find(value.economies, civilization_id,
                                         &CivilizationEconomy::civilization_id);
  if (expected.at("Credits").is_null())
    require(economy == value.economies.end(),
            std::string(name) + " economy unexpectedly exists.");
  else
    require(economy != value.economies.end() &&
                same(economy->credits, number(expected.at("Credits"))),
            std::string(name) + " credits differed.");
  const auto *state = value.campaign.try_get_civilization(civilization_id);
  if (expected.at("StateRevision").is_null()) {
    require(!state, std::string(name) + " state unexpectedly exists.");
    return;
  }
  require(state && state->revision() ==
                       expected.at("StateRevision").get<std::int64_t>(),
          std::string(name) + " state revision differed.");
  const auto project = state
                           ? std::ranges::find(state->active_projects(), node_id,
                                               &ResearchProjectRuntimeState::node_id)
                           : std::span<const ResearchProjectRuntimeState>{}.end();
  if (expected.at("Project").is_null())
    require(!state || project == state->active_projects().end(),
            std::string(name) + " project unexpectedly exists.");
  else
    require(state && project != state->active_projects().end() &&
                project_json(*project) == expected.at("Project"),
            std::string(name) + " project differed.");
  require(funding_json(value.campaign.project_funding(civilization_id)) ==
              expected.at("Funding"),
          std::string(name) + " funding state/order differed.");
}

Json result_json(const AdaptiveResearchCommandResult &value) {
  Json events = Json::array();
  for (const auto &event : value.events)
    events.push_back({{"Type", static_cast<int>(event.type)},
                      {"CivilizationId", event.civilization_id},
                      {"NodeId", event.node_id ? Json(*event.node_id) : Json(nullptr)},
                      {"SubjectId", event.subject_id ? Json(*event.subject_id)
                                                      : Json(nullptr)},
                      {"Message", event.message}});
  Json blockers = Json::array();
  for (const auto &blocker : value.blockers)
    blockers.push_back({{"Code", static_cast<int>(blocker.code)},
                        {"SubjectId", blocker.subject_id
                                          ? Json(*blocker.subject_id)
                                          : Json(nullptr)},
                        {"RequiredValue", blocker.required_value
                                              ? Json(*blocker.required_value)
                                              : Json(nullptr)},
                        {"ActualValue", blocker.actual_value
                                            ? Json(*blocker.actual_value)
                                            : Json(nullptr)},
                        {"Message", blocker.message}});
  return {{"Accepted", value.accepted}, {"Message", value.message},
          {"Events", std::move(events)}, {"Blockers", std::move(blockers)}};
}


void cancellation_stage_retention(const AdaptiveResearchStrategicRuntime &runtime) {
  using Access = detail::AdaptiveResearchCampaignStateAccess;
  const auto &kernel=runtime.authority().kernel();
  for(const auto desired:{ResearchMaturity::experimental,ResearchMaturity::demonstrated,ResearchMaturity::engineering}){
    auto value=setup(runtime);
    auto &state=Access::get_civilization(value.campaign,1);
    for(const auto &id:kernel.facilities().facility_capability_ids())kernel.add_facility_capability(state,id);
    const auto view=kernel.build_view(state,"species:terran_baseline");
    const auto candidate=std::ranges::find_if(view.visible_nodes,[&](const auto &n){
      return n.state==ResearchMaturity::investigable && n.blockers.empty() && n.minimum_labs &&
          !kernel.catalog().get_node(n.node_id).is_hypothesis;
    });
    require(candidate!=view.visible_nodes.end(),"No ordinary candidate for stage retention.");
    const auto node_id=candidate->node_id;
    const auto &node=kernel.catalog().get_node(node_id);
    const double labs=*candidate->minimum_labs;
    require(AdaptiveResearchCampaignCommands::start_directed_research({value.civilizations,value.economies},value.campaign,1,node_id,labs,"species:terran_baseline").accepted,"Stage start failed.");
    const auto initial=*std::ranges::find(state.active_projects(),node_id,&ResearchProjectRuntimeState::node_id);
    double work=0.;
    for(const auto stage:{ResearchMaturity::experimental,ResearchMaturity::demonstrated,ResearchMaturity::engineering}){
      work+=kernel.progress_policy().get_stage_work(node,stage)*(stage==desired?.1:1.);
      if(stage==desired)break;
    }
    const double annual=kernel.catalog().lab_scaling().scale_assigned_labs(labs,node.project_requirements.recommended_labs)*
        kernel.catalog().metadata().base_rp_per_effective_lab_per_year*initial.readiness_efficiency;
    (void)kernel.advance_projects(state,work/annual);
    const auto retained=*std::ranges::find(state.active_projects(),node_id,&ResearchProjectRuntimeState::node_id);
    require(retained.stage==desired,"Progress fixture did not reach its requested stage.");
    const std::vector<ResearchCapabilityKey> capabilities(state.capabilities().begin(),state.capabilities().end());
    require(AdaptiveResearchCampaignCommands::cancel_directed_research({value.civilizations,value.economies},value.campaign,1,node_id).accepted,"Stage cancellation failed.");
    require(std::ranges::equal(capabilities,state.capabilities()),"Cancellation removed an earned capability.");
    require(AdaptiveResearchCampaignCommands::start_directed_research({value.civilizations,value.economies},value.campaign,1,node_id,labs,"species:terran_baseline").accepted,"Stage restart failed.");
    const auto &restarted=*std::ranges::find(state.active_projects(),node_id,&ResearchProjectRuntimeState::node_id);
    require(restarted.stage==desired && restarted.total_research_points==retained.total_research_points &&
        restarted.stage_research_points==retained.stage_research_points,"Restart reset an earned stage or its work.");
  }
}

void cancellation_contract(const AdaptiveResearchStrategicRuntime &runtime) {
  using Access = detail::AdaptiveResearchCampaignStateAccess;
  const auto &authority = runtime.authority();
  AdaptiveResearchSnapshotCodec core_codec(authority.catalog(), authority.kernel().applicability(), authority.kernel().facilities());
  for (int consumed_count = 0; consumed_count <= 3; ++consumed_count) {
    auto value = setup(runtime);
    auto &state = Access::get_civilization(value.campaign, 1);
    const AdaptiveResearchFundingWorldView world{value.civilizations, value.economies};
    auto start = AdaptiveResearchCampaignCommands::start_directed_research(world, value.campaign, 1,
        value.node_id, value.labs, "species:terran_baseline");
    require(start.accepted, "Cancellation setup could not start: " + start.message);
    (void)authority.kernel().advance_projects(state, .001);
    if (consumed_count == 1)
      require(AdaptiveResearchCampaignCommands::pause_directed_research(value.campaign, 1, value.node_id).accepted,
          "Cancellation pause setup failed.");
    for (int i=0;i<consumed_count;++i)
      (void)Access::consume_project_milestone(value.campaign, 1, value.node_id, i==2);
    const auto retained = *std::ranges::find(state.active_projects(), value.node_id, &ResearchProjectRuntimeState::node_id);
    const auto refund = *AdaptiveResearchCampaignCommands::cancellation_refund(value.campaign, 1, value.node_id);
    const double credits = value.economies.front().credits;
    const double free_labs = state.free_effective_labs();
    const auto before = core_codec.serialize(state);
    auto missing = AdaptiveResearchCampaignCommands::cancel_directed_research({value.civilizations,{}},value.campaign,1,value.node_id);
    require(!missing.accepted && core_codec.serialize(state)==before, "Missing treasury mutated research.");
    auto result = AdaptiveResearchCampaignCommands::cancel_directed_research(world, value.campaign, 1, value.node_id);
    require(result.accepted && result.events.front().type==AdaptiveResearchRuntimeEventType::project_cancelled,
        "Canonical cancellation failed.");
    require(value.economies.front().credits==credits+refund && value.campaign.project_funding(1).empty(),
        "Cancellation refunded spent costs or retained a reservation.");
    require(state.active_projects().empty() && state.cancelled_project(value.node_id) &&
        state.cancelled_project(value.node_id)->total_research_points==retained.total_research_points &&
        state.cancelled_project(value.node_id)->target_applicability_context_id==retained.target_applicability_context_id &&
        state.free_effective_labs()==free_labs+(retained.paused?0:value.labs),
        "Cancellation failed to free labs or preserve work/context.");
    const auto saved = core_codec.serialize(state);
    require(Json::parse(saved).at("schemaVersion")==6,"Cancelled work did not use the new core snapshot format.");
    auto restored = core_codec.deserialize(saved);
    require(core_codec.serialize(restored)==saved,"Cancelled scientific work did not round trip.");
    (void)authority.kernel().advance_projects(restored, 10.);
    require(core_codec.serialize(restored)==saved,"Cancelled research continued progressing.");
    AdaptiveResearchCampaignSnapshotCodec campaign_codec(runtime);
    auto loaded = campaign_codec.restore(value.civilizations, campaign_codec.capture(value.campaign));
    auto &loaded_state = Access::get_civilization(loaded,1);
    const auto repeated = AdaptiveResearchCampaignCommands::cancel_directed_research(world,loaded,1,value.node_id);
    require(!repeated.accepted && value.economies.front().credits==credits+refund,
        "Repeated cancellation after reload generated another refund.");
    auto wrong_context = AdaptiveResearchCampaignCommands::start_directed_research(world,loaded,1,value.node_id,value.labs,std::nullopt);
    require(!wrong_context.accepted && loaded_state.cancelled_project(value.node_id),
        "Restart transferred scientific work into a different context.");
    const auto quote = AdaptiveResearchFundingPolicy::quote(authority.catalog().get_node(value.node_id),value.labs,authority.catalog());
    const auto restarted = AdaptiveResearchCampaignCommands::start_directed_research(world,loaded,1,value.node_id,value.labs,"species:terran_baseline");
    require(restarted.accepted && loaded_state.cancelled_projects().empty() &&
        loaded_state.active_projects().front().stage==retained.stage &&
        loaded_state.active_projects().front().stage_research_points==retained.stage_research_points &&
        std::abs(value.economies.front().credits-(credits+refund-quote.authorization_credits-quote.milestone_commitment_credits))<1e-9,
        "Restart failed to preserve scientific work or fund its new commitment.");
    require(Json::parse(core_codec.serialize(loaded_state)).at("schemaVersion")==1,
        "Campaign without cancellation records no longer uses legacy-compatible encoding.");
    for(int fault=0;fault<4;++fault){
      auto invalid=Json::parse(saved);
      auto &project=invalid["cancelledProjects"][0];
      if(fault==0)invalid["cancelledProjects"].push_back(project);
      if(fault==1)invalid["activeProjects"].push_back(project);
      if(fault==2)project["totalResearchPoints"]=-1.;
      if(fault==3)project["targetApplicabilityContextId"]="species:missing";
      bool rejected=false;try{(void)core_codec.deserialize(invalid.dump());}catch(const std::exception&){rejected=true;}
      require(rejected,"Invalid cancelled-project snapshot was accepted.");
    }
  }
}

} // namespace

int main(int argc, char **argv) try {
  if (argc != 3)
    throw std::invalid_argument(
        "Expected actual-source fixture and canonical research directory.");
  const auto fixture_path = std::filesystem::absolute(argv[1]);
  const auto research_path = std::filesystem::absolute(argv[2]);
  const auto fixture = Json::parse(read(fixture_path));
  const auto canonical = fingerprint(research_path);
  require(canonical == fixture.at("CanonicalFingerprint").get<std::string>(),
          "Canonical fingerprint differed.");
  auto runtime = load_adaptive_research_strategic_runtime(research_path);
  require(fingerprint(research_path) == canonical,
          "Runtime load changed canonical inputs.");

  cancellation_stage_retention(runtime);
  cancellation_contract(runtime);
  std::size_t count{};
  for (const auto &row : fixture.at("Rows")) {
    const auto name = row.at("Name").get<std::string>();
    const auto kind = row.at("Kind").get<std::string>();
    require(row.at("BeforeFingerprint").get<std::string>() == canonical &&
                row.at("AfterFingerprint").get<std::string>() == canonical,
            name + " source input fingerprints differed.");
    if (kind == "quote") {
      auto node = runtime.authority().catalog().get_node(
          row.at("NodeId").get<std::string>());
      const auto &input = row.at("Node");
      node.complexity = input.at("Complexity").get<std::string>();
      node.project_requirements.base_research_points =
          input.at("BaseResearchPoints").get<double>();
      node.project_requirements.recommended_labs =
          input.at("RecommendedLabs").get<int>();
      std::optional<AdaptiveResearchFundingQuote> quote;
      const auto error = invoke([&] {
        quote.emplace(AdaptiveResearchFundingPolicy::quote(
            node, number(row.at("Labs")), runtime.authority().catalog()));
      });
      compare_error(row.at("Error"), error, name);
      if (quote) compare_quote(row.at("Quote"), *quote, name);
    } else if (kind == "complexity") {
      struct Values { double authorization{}, milestone{}, multiplier{}; } values;
      const auto input = row.at("Input").get<std::string>();
      const auto error = invoke([&] {
        values.authorization =
            AdaptiveResearchFundingPolicy::authorization_credits(input);
        values.milestone =
            AdaptiveResearchFundingPolicy::milestone_commitment_credits(input);
        values.multiplier =
            AdaptiveResearchFundingPolicy::complexity_multiplier(input);
      });
      compare_error(row.at("Error"), error, name);
      if (!error) {
        const auto &expected = row.at("Value");
        require(values.authorization == expected.at("Authorization").get<double>() &&
                    values.milestone == expected.at("Milestone").get<double>() &&
                    values.multiplier == expected.at("Multiplier").get<double>(),
                name + " complexity policy values differed.");
      }
    } else if (kind == "runway") {
      std::optional<double> value;
      const auto error = invoke([&] {
        value.emplace(AdaptiveResearchFundingPolicy::estimate_treasury_runway_days(
            number(row.at("Available")), number(row.at("BeforeResearch")),
            number(row.at("Research"))));
      });
      compare_error(row.at("Error"), error, name);
      if (value)
        require(same(*value, number(row.at("Value"))),
                name + " runway differed.");
    } else if (kind == "credits-needed") {
      const auto &input = row.at("Input");
      AdaptiveResearchFundingQuote quote{
          number(input.at("AssignedEffectiveLabs")),
          number(input.at("AuthorizationCredits")),
          number(input.at("MilestoneCommitmentCredits")),
          number(input.at("OperatingCreditsPerDay")),
          number(input.at("EstimatedTotalOperatingCredits")),
          number(input.at("EstimatedTotalCredits")),
          number(input.at("EstimatedYearsAtFullFunding")),
          input.at("Complexity").get<std::string>()};
      const auto actual =
          AdaptiveResearchCampaignCommands::credits_needed_to_start(quote);
      require(same(actual, number(row.at("Value"))),
              name + " start credit sum differed.");
    } else if (kind == "command") {
      auto value = setup(runtime);
      const auto operation = row.at("Operation").get<std::string>();
      const auto civilization_id = row.at("CivilizationId").get<int>();
      const auto node_id = row.at("NodeId").get<std::string>();
      const auto labs = number(row.at("Labs"));
      if (name == "start-missing-economy") value.economies.clear();
      else if (name == "start-duplicate-economy")
        value.economies.push_back({1, 600});
      else if (name == "start-unknown-campaign-civilization") {
        value.economies.clear();
        value.economies.push_back({2, 500});
      } else if (name == "start-insufficient-credits")
        value.economies.front().credits = 0;
      else if (name == "start-affordability-epsilon") {
        const auto quote = AdaptiveResearchFundingPolicy::quote(
            runtime.authority().catalog().get_node(node_id), labs,
            runtime.authority().catalog());
        value.economies.front().credits =
            AdaptiveResearchCampaignCommands::credits_needed_to_start(quote) -
            0.0000005;
      }
      const bool needs_started =
          name == "start-duplicate-funding" || name == "pause-success" ||
          name.starts_with("resume-") && name != "resume-not-paused";
      if (needs_started) {
        const auto started = AdaptiveResearchCampaignCommands::start_directed_research(
            {value.civilizations, value.economies}, value.campaign, 1,
            value.node_id, value.labs, "species:terran_baseline");
        require(started.accepted, name + " setup start failed.");
      }
      if (name.starts_with("resume-") && name != "resume-not-paused" &&
          name != "resume-hypothesis-before-economy") {
        auto paused = AdaptiveResearchCampaignCommands::pause_directed_research(
            value.campaign, 1, value.node_id);
        require(paused.accepted, name + " setup pause failed.");
      }
      if (name == "resume-insufficient-credits")
        value.economies.front().credits = 0;
      else if (name == "resume-missing-economy") value.economies.clear();
      else if (name == "resume-duplicate-economy")
      {
        value.economies.front().credits = 500;
        value.economies.push_back({1, 600});
      }
      else if (name == "resume-hypothesis-before-economy") {
        auto &state = detail::AdaptiveResearchCampaignStateAccess::get_civilization(
            value.campaign, 1);
        auto project = *std::ranges::find(state.active_projects(), value.node_id,
                                          &ResearchProjectRuntimeState::node_id);
        project.paused = true;
        project.pause_reason = "hypothesis_resolution_required";
        detail::AdaptiveResearchStateWriter::set_project(state, project);
        value.economies.clear();
      }
      compare_campaign(row.at("Before"), value, civilization_id, node_id,
                       name + " before");
      std::optional<AdaptiveResearchCommandResult> result;
      const auto error = invoke([&] {
        if (operation == "start")
          result.emplace(AdaptiveResearchCampaignCommands::start_directed_research(
              {value.civilizations, value.economies}, value.campaign,
              civilization_id, node_id, labs, "species:terran_baseline"));
        else if (operation == "pause")
          result.emplace(AdaptiveResearchCampaignCommands::pause_directed_research(
              value.campaign, civilization_id, node_id));
        else
          result.emplace(AdaptiveResearchCampaignCommands::resume_directed_research(
              {value.civilizations, value.economies}, value.campaign,
              civilization_id, node_id, labs));
      });
      compare_error(row.at("Error"), error, name);
      if (result)
        require(result_json(*result) == row.at("Result"),
                name + " command result differed.");
      compare_campaign(row.at("After"), value, civilization_id, node_id,
                       name + " after");
    }
    ++count;
  }
  require(count == fixture.at("RowCount").get<std::size_t>(),
          "Not every source row was replayed.");
  require(fingerprint(research_path) ==
              fixture.at("FinalFingerprint").get<std::string>(),
          "Native replay changed canonical inputs.");
  std::cout << "Adaptive Research funding parity passed " << count
            << " actual-source rows.\n";
  return 0;
} catch (const std::exception &error) {
  std::cerr << error.what() << '\n'
            << "exception=" << typeid(error).name() << '\n'
            << "cwd=" << std::filesystem::current_path().string() << '\n'
            << "fixture=" << (argc > 1 ? argv[1] : "<missing>") << '\n'
            << "researchRoot=" << (argc > 2 ? argv[2] : "<missing>") << '\n';
  return 1;
}
