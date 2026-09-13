#include <algorithm>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <nlohmann/json.hpp>
#include <optional>
#include <stellar/core/adaptive_research_campaign.hpp>
#include <stellar/core/detail/adaptive_research_campaign_state_access.hpp>
#include <stellar/core/detail/adaptive_research_outcome_snapshot_json.hpp>
#include <stellar/core/detail/adaptive_research_sha256.hpp>
#include <typeinfo>
#include <vector>
using namespace stellar::core;
using J = nlohmann::ordered_json;
namespace fs = std::filesystem;
std::string fingerprint(const fs::path &root) {
  std::vector<fs::path> paths;
  for (const auto &entry : fs::directory_iterator(fs::absolute(root)))
    if (entry.is_regular_file() && entry.path().extension() == ".json")
      paths.push_back(entry.path());
  std::sort(paths.begin(), paths.end(), [](const auto &a, const auto &b) {
    return a.filename().string() < b.filename().string();
  });
  std::vector<std::uint8_t> bytes;
  for (const auto &path : paths) {
    const auto name = path.filename().string();
    bytes.insert(bytes.end(), name.begin(), name.end());
    std::ifstream stream(path, std::ios::binary);
    if (!stream)
      throw std::runtime_error("Could not read canonical research file: " + path.string());
    const std::string content((std::istreambuf_iterator<char>(stream)), {});
    bytes.insert(bytes.end(), content.begin(), content.end());
  }
  const auto digest = detail::adaptive_research_sha256(bytes);
  constexpr char hex[] = "0123456789ABCDEF";
  std::string result;
  for (const auto value : digest) {
    result.push_back(hex[value >> 4]);
    result.push_back(hex[value & 15]);
  }
  return result;
}
J funding(const AdaptiveResearchProjectFundingState &v) {
  return {{"nodeId", v.node_id},
          {"reservedMilestoneCredits", v.reserved_milestone_credits},
          {"consumedMilestoneCredits", v.consumed_milestone_credits},
          {"authorizationCredits", v.authorization_credits}};
}
J snap(const AdaptiveResearchCampaignSnapshot &s) {
  J cs = J::array();
  for (auto &e : s.civilizations) {
    J fs = J::array();
    for (auto &f : e.project_funding)
      fs.push_back({{"nodeId", f.node_id},
                    {"reservedMilestoneCredits", f.reserved_milestone_credits},
                    {"consumedMilestoneCredits", f.consumed_milestone_credits},
                    {"authorizationCredits", f.authorization_credits}});
    cs.push_back(
        {{"civilizationId", e.civilization_id},
         {"speciesId", e.species_id},
         {"referenceProfileId", e.reference_profile_id},
         {"applicabilityContextId", e.applicability_context_id},
         {"research", J::parse(detail::encode_adaptive_research_snapshot_v5_dto(e.research))},
         {"projectFunding", fs}});
  }
  return {{"schemaVersion", s.schema_version}, {"catalogId", s.catalog_id}, {"civilizations", cs}};
}
AdaptiveResearchCampaignSnapshot decode(const J &j) {
  AdaptiveResearchCampaignSnapshot s{
      j.at("schemaVersion").get<int>(), j.at("catalogId").get<std::string>(), {}};
  for (auto &e : j.at("civilizations")) {
    AdaptiveResearchCampaignCivilizationSnapshot c{
        e.at("civilizationId").get<int>(),
        e.at("speciesId").get<std::string>(),
        e.at("referenceProfileId").get<std::string>(),
        e.at("applicabilityContextId").get<std::string>(),
        detail::decode_adaptive_research_snapshot_v5_dto(e.at("research").dump()),
        {}};
    if (e.contains("projectFunding") && !e.at("projectFunding").is_null())
      for (auto &f : e.at("projectFunding"))
        c.project_funding.push_back(
            {f.at("nodeId").get<std::string>(), f.at("reservedMilestoneCredits").get<double>(),
             f.at("consumedMilestoneCredits").get<double>(), f.value("authorizationCredits", 0.)});
    s.civilizations.push_back(std::move(c));
  }
  return s;
}
J projection(const AdaptiveResearchCampaignSnapshotCodec &codec,
             const AdaptiveResearchCampaignState &c) {
  J ids = J::array(), starts = J::array(), revs = J::array(), fund = J::array();
  for (int id : c.civilization_ids()) {
    ids.push_back(id);
    auto &st = c.get_start(id);
    starts.push_back({{"CivilizationId", st.civilization_id},
                      {"SpeciesId", st.species_id},
                      {"ReferenceProfileId", st.reference_profile_id},
                      {"ApplicabilityContextId", st.applicability_context_id}});
    auto &s = c.get_civilization(id);
    revs.push_back({{"CivilizationId", s.civilization_id()},
                    {"Revision", s.revision()},
                    {"MaterializedViewRevision", s.materialized_view_revision()},
                    {"ExpertiseRevision", s.expertise().revision()}});
    J vals = J::array();
    for (auto &f : c.project_funding(id))
      vals.push_back({{"NodeId", f.node_id},
                      {"ReservedMilestoneCredits", f.reserved_milestone_credits},
                      {"ConsumedMilestoneCredits", f.consumed_milestone_credits},
                      {"AuthorizationCredits", f.authorization_credits}});
    fund.push_back({{"Id", id}, {"Values", vals}});
  }
  return {{"Ids", ids},
          {"Starts", starts},
          {"Capture", snap(codec.capture(c))},
          {"StateRevisions", revs},
          {"Funding", fund}};
}
std::vector<Civilization> civs(const J &a) {
  std::vector<Civilization> r;
  for (auto &v : a) {
    Civilization c{};
    c.id = v.at("Id").get<int>();
    c.species_id = v.at("SpeciesId").get<std::string>();
    r.push_back(std::move(c));
  }
  return r;
}
J err(std::exception_ptr p) {
  if (!p)
    return nullptr;
  try {
    std::rethrow_exception(p);
  } catch (const AdaptiveResearchCampaignArgumentError &e) {
    return {{"Type", "ArgumentException"}, {"Message", e.what()}};
  } catch (const AdaptiveResearchCampaignMissingState &e) {
    return {{"Type", "KeyNotFoundException"}, {"Message", e.what()}};
  } catch (const AdaptiveResearchCampaignDataError &e) {
    return {{"Type", "InvalidDataException"}, {"Message", e.what()}};
  } catch (const AdaptiveResearchCampaignOperationError &e) {
    return {{"Type", "InvalidOperationException"}, {"Message", e.what()}};
  } catch (const std::exception &e) {
    return {{"Type", "UnexpectedNativeException"}, {"Message", e.what()}};
  }
}
std::pair<AdaptiveResearchCampaignState, std::string>
active(const AdaptiveResearchStrategicRuntime &rt, const AdaptiveResearchCampaignFactory &factory) {
  Civilization c{};
  c.id = 1;
  c.species_id = "terran_baseline";
  auto campaign = factory.create(std::span<const Civilization>(&c, 1));
  auto &state = detail::AdaptiveResearchCampaignStateAccess::get_civilization(campaign, 1);
  auto view = rt.authority().build_view(state);
  for (auto &n : view.visible_nodes)
    if (n.state == ResearchMaturity::investigable && n.blockers.empty() && n.minimum_labs) {
      auto r = rt.authority().start_directed_research(state, n.node_id, *n.minimum_labs,
                                                      std::string_view("species:terran_baseline"));
      if (!r.accepted)
        throw std::runtime_error(r.message);
      return {std::move(campaign), n.node_id};
    }
  throw std::runtime_error("no candidate");
}
J consumed(const detail::AdaptiveResearchMilestoneConsumption &r) {
  return {{"Found", r.found}, {"Consumed", r.consumed_credits}, {"Remaining", r.remaining_credits}};
}
int run(int argc, char **argv) {
  if (argc != 3)
    throw std::invalid_argument("Expected canonical research directory and fixture path.");
  const auto before_fingerprint = fingerprint(argv[1]);
  std::ifstream in(argv[2]);
  if (!in)
    throw std::runtime_error("Could not read fixture file.");
  J fixture = J::parse(in);
  if (fixture.at("CanonicalFingerprint").get<std::string>() != before_fingerprint)
    throw std::runtime_error("Canonical research fingerprint does not match fixture.");
  auto rt = load_adaptive_research_strategic_runtime(argv[1]);
  AdaptiveResearchCampaignFactory factory(rt);
  AdaptiveResearchCampaignSnapshotCodec codec(rt);
  int count = 0;
  for (auto &row : fixture.at("Rows")) {
    auto name = row.at("Name").get<std::string>();
    auto kind = row.at("Kind").get<std::string>();
    if (kind == "capture") {
      auto other = load_adaptive_research_strategic_runtime(argv[1]);
      AdaptiveResearchCampaignSnapshotCodec other_codec(other);
      Civilization c{};
      c.id = 1;
      c.species_id = "terran_baseline";
      auto campaign = factory.create(std::span<const Civilization>(&c, 1));
      std::exception_ptr ep;
      try {
        (void)other_codec.capture(campaign);
      } catch (...) {
        ep = std::current_exception();
      }
      if (err(ep) != row.at("Error")) {
        std::cerr << name << " capture mismatch\n";
        return 1;
      }
      ++count;
      continue;
    }
    if (kind == "funding") {
      auto [campaign, node] = active(rt, factory);
      std::exception_ptr ep;
      if (name == "funding-sequence" || name == "reserve-duplicate") {
        detail::AdaptiveResearchCampaignStateAccess::reserve_project_milestones(campaign, 1, node,
                                                                                2.5, 9);
        detail::AdaptiveResearchCampaignStateAccess::reserve_project_milestones(
            campaign, 1, "second-slot", 1, 6);
        const auto retained_projection = projection(codec, campaign);
        const auto retained_projection_bytes = retained_projection.dump();
        const auto retained_capture = codec.capture(campaign);
        const auto retained_capture_bytes = snap(retained_capture).dump();
        const J *steps = name == "funding-sequence" ? &row.at("Steps") : nullptr;
        size_t si = 0;
        std::vector<std::pair<J, J>> deferred_steps;
        auto check = [&](J result) {
          auto state = projection(codec, campaign);
          if (steps && (result != (*steps)[si].at("Result") || state != (*steps)[si].at("State"))) {
            std::cerr << name << " step mismatch " << si << "\n";
            throw std::runtime_error("step mismatch");
          }
          deferred_steps.emplace_back(result, state);
          ++si;
        };
        check(consumed(detail::AdaptiveResearchCampaignStateAccess::consume_project_milestone(
            campaign, 1, node, false)));
        check(consumed(detail::AdaptiveResearchCampaignStateAccess::consume_project_milestone(
            campaign, 1, node, true)));
        check(consumed(detail::AdaptiveResearchCampaignStateAccess::consume_project_milestone(
            campaign, 1, "second-slot", true)));
        check(consumed(detail::AdaptiveResearchCampaignStateAccess::consume_project_milestone(
            campaign, 1, node, false)));
        detail::AdaptiveResearchCampaignStateAccess::reserve_project_milestones(
            campaign, 1, "reinsert-a", 4, 12);
        detail::AdaptiveResearchCampaignStateAccess::reserve_project_milestones(campaign, 1, node,
                                                                                5, 15);
        check(nullptr);
        if (name == "reserve-duplicate") {
          try {
            detail::AdaptiveResearchCampaignStateAccess::reserve_project_milestones(campaign, 1,
                                                                                    node, 1, 1);
          } catch (...) {
            ep = std::current_exception();
          }
          if (err(ep) != row.at("Error") || projection(codec, campaign) != row.at("Result")) {
            std::cerr << name << " mismatch\n";
            return 1;
          }
        }
        if (retained_projection.dump() != retained_projection_bytes ||
            snap(retained_capture).dump() != retained_capture_bytes) {
          std::cerr << name << " retained ownership changed\n";
          return 1;
        }
        if (steps) {
          for (std::size_t index = 0; index < deferred_steps.size(); ++index) {
            if (deferred_steps[index].first != (*steps)[index].at("Result") ||
                deferred_steps[index].second != (*steps)[index].at("State")) {
              std::cerr << name << " deferred step mismatch " << index << "\n";
              return 1;
            }
          }
        }
      } else {
        std::vector<AdaptiveResearchProjectFundingSnapshot> f{{node, 3, 1, 2},
                                                              {"inactive", 1, 0, 0}};
        try {
          detail::AdaptiveResearchCampaignStateAccess::restore_project_funding(campaign, 1, f);
        } catch (...) {
          ep = std::current_exception();
        }
        if (err(ep) != row.at("Error") || projection(codec, campaign) != row.at("Result")) {
          std::cerr << name << " mismatch\n";
          return 1;
        }
      }
      ++count;
      continue;
    }
    auto input_civs = civs(kind == "factory" ? row.at("Input") : row.at("Galaxy"));
    std::optional<AdaptiveResearchCampaignSnapshot> input_snapshot;
    if (kind != "factory")
      input_snapshot = decode(row.at("Input"));
    const auto input_before = input_snapshot ? snap(*input_snapshot).dump() : std::string{};
    std::exception_ptr ep;
    std::optional<AdaptiveResearchCampaignState> result;
    try {
      if (kind == "factory")
        result.emplace(factory.create(input_civs));
      else
        result.emplace(codec.restore(input_civs, *input_snapshot));
    } catch (...) {
      ep = std::current_exception();
    }
    if (err(ep) != row.at("Error")) {
      std::cerr << name << " error mismatch\n"
                << err(ep).dump() << "\n"
                << row.at("Error").dump() << "\n";
      return 1;
    }
    if (result && projection(codec, *result) != row.at("Result")) {
      std::cerr << name << " result mismatch\n";
      return 1;
    }
    ++count;
    if (input_snapshot && snap(*input_snapshot).dump() != input_before) {
      std::cerr << name << " input mutated\n";
      return 1;
    }
  }
  {
    auto [moving, node] = active(rt, factory);
    auto *original = &moving.get_civilization(1);
    AdaptiveResearchCampaignState moved(std::move(moving));
    if (&moved.get_civilization(1) != original) {
      std::cerr << "move changed state address\n";
      return 1;
    }
    auto [target, target_node] = active(rt, factory);
    auto *replaced = &target.get_civilization(1);
    target = std::move(moved);
    if (&target.get_civilization(1) != original || &target.get_civilization(1) == replaced) {
      std::cerr << "move assignment identity mismatch\n";
      return 1;
    }
    detail::AdaptiveResearchCampaignStateAccess::reserve_project_milestones(target, 1, node, 1, 3);
    auto span = target.project_funding(1);
    std::string_view aliased = span[0].node_id;
    auto consumed_value = detail::AdaptiveResearchCampaignStateAccess::consume_project_milestone(
        target, 1, aliased, true);
    if (!consumed_value.found || !target.project_funding(1).empty()) {
      std::cerr << "aliased consume failed\n";
      return 1;
    }
  }
  if (count != static_cast<int>(fixture.at("Rows").size()))
    throw std::runtime_error("Native replay did not invoke every fixture row.");
  if (fingerprint(argv[1]) != before_fingerprint)
    throw std::runtime_error("Native replay changed canonical research inputs.");
  std::cout << "adaptive_research_campaign_tests: " << count << " core source rows passed\n";
  return 0;
}
int main(int argc, char **argv) {
  try {
    return run(argc, argv);
  } catch (const std::exception &error) {
    std::cerr << "ExceptionType: " << typeid(error).name() << "\nMessage: " << error.what()
              << "\nCurrentDirectory: " << fs::current_path().string()
              << "\nResearchRoot: " << (argc > 1 ? argv[1] : "<missing>")
              << "\nFixturePath: " << (argc > 2 ? argv[2] : "<missing>") << "\n";
    return 1;
  } catch (...) {
    std::cerr << "ExceptionType: unknown\nMessage: non-standard exception"
              << "\nCurrentDirectory: " << fs::current_path().string()
              << "\nResearchRoot: " << (argc > 1 ? argv[1] : "<missing>")
              << "\nFixturePath: " << (argc > 2 ? argv[2] : "<missing>") << "\n";
    return 1;
  }
}
