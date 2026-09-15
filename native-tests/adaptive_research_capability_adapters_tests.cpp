#include <stellar/core/adaptive_research_capability_adapters.hpp>

#include <stellar/core/detail/adaptive_research_campaign_state_access.hpp>
#include <stellar/core/detail/adaptive_research_sha256.hpp>
#include <stellar/core/detail/adaptive_research_state_writer.hpp>

#include <nlohmann/json.hpp>

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <optional>
#include <span>
#include <stdexcept>
#include <string>
#include <type_traits>
#include <utility>
#include <vector>

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
  result.reserve(value.size() * 2);
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
  std::string bytes;
  for (const auto &path : files) {
    bytes += path.filename().string();
    bytes += read(path);
  }
  return hex(detail::adaptive_research_sha256(
      {reinterpret_cast<const std::uint8_t *>(bytes.data()), bytes.size()}));
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

AdaptiveResearchCampaignState
campaign(const AdaptiveResearchStrategicRuntime &runtime, const Json &row) {
  const std::vector<Civilization> civilizations{civilization()};
  AdaptiveResearchCampaignFactory factory(runtime);
  auto result = factory.create(civilizations);
  auto &state = detail::AdaptiveResearchCampaignStateAccess::get_civilization(
      result, 1);

  const std::vector<ResearchCapabilityKey> capabilities(
      state.capabilities().begin(), state.capabilities().end());
  for (const auto &capability : capabilities)
    require(detail::AdaptiveResearchStateWriter::remove_capability(
                state, capability),
            "Could not clear starting capability.");
  std::vector<std::string> node_ids;
  for (const auto &node : state.node_states()) node_ids.push_back(node.node_id);
  for (const auto &node_id : node_ids)
    require(detail::AdaptiveResearchStateWriter::remove_node_state(
                state, node_id),
            "Could not clear starting node.");

  if (!row.at("GrantedCapability").is_null()) {
    ResearchCapabilityKey key;
    key.capability_id = row.at("GrantedCapability").get<std::string>();
    if (!row.at("CapabilityContext").is_null())
      key.context_id = row.at("CapabilityContext").get<std::string>();
    require(detail::AdaptiveResearchStateWriter::add_capability(
                state, std::move(key)),
            "Could not add retained capability.");
  }
  if (!row.at("EstablishedNode").is_null()) {
    ResearchNodeRuntimeState node;
    node.node_id = row.at("EstablishedNode").get<std::string>();
    node.maturity = static_cast<ResearchMaturity>(
        row.at("Maturity").get<int>());
    if (!row.at("Resolution").is_null())
      node.resolution = row.at("Resolution").get<std::string>();
    node.revision = 1;
    detail::AdaptiveResearchStateWriter::set_node_state(state, std::move(node));
  }
  return result;
}

Json state_view(const AdaptiveResearchCampaignState &campaign_state) {
  const auto &state = campaign_state.get_civilization(1);
  Json capabilities = Json::array();
  for (const auto &value : state.capabilities())
    capabilities.push_back(
        {{"CapabilityId", value.capability_id},
         {"ContextId", value.context_id ? Json(*value.context_id)
                                         : Json(nullptr)}});
  Json nodes = Json::array();
  for (const auto &value : state.node_states())
    nodes.push_back({{"NodeId", value.node_id},
                     {"Maturity", static_cast<int>(value.maturity)},
                     {"Resolution", value.resolution ? Json(*value.resolution)
                                                       : Json(nullptr)},
                     {"Revision", value.revision}});
  return {{"Capabilities", std::move(capabilities)},
          {"Nodes", std::move(nodes)}};
}

struct Error {
  std::string type;
  std::string message;
};

template <class Call>
std::optional<Error> invoke(Call &&call) {
  try {
    std::forward<Call>(call)();
  } catch (const AdaptiveResearchCampaignMissingState &error) {
    return Error{"KeyNotFoundException", error.what()};
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
          std::string(name) + " exception type differed.");
  require(actual->message == expected.at("Message").get<std::string>(),
          std::string(name) + " exception message differed: " +
              actual->message);
}

} // namespace

static_assert(!std::is_constructible_v<
              AdaptiveResearchConstructionCapabilityView,
              AdaptiveResearchCampaignState &&>);
static_assert(!std::is_constructible_v<
              AdaptiveResearchShipbuildingCapabilityView,
              AdaptiveResearchCampaignState &&>);

int main(int argc, char **argv) {
  try {
    if (argc != 3)
      throw std::invalid_argument(
          "Expected fixture path and canonical research directory.");
    const auto fixture_path = std::filesystem::absolute(argv[1]);
    const auto research_root = std::filesystem::absolute(argv[2]);
    const auto fixture = Json::parse(read(fixture_path));
    const auto expected_fingerprint =
        fixture.at("CanonicalFingerprint").get<std::string>();
    require(fingerprint(research_root) == expected_fingerprint,
            "Canonical input fingerprint differed before loading.");
    auto runtime = load_adaptive_research_strategic_runtime(research_root);
    require(fingerprint(research_root) == expected_fingerprint,
            "Canonical input fingerprint differed after loading.");

    std::size_t count = 0;
    for (const auto &row : fixture.at("Rows")) {
      const auto name = row.at("Name").get<std::string>();
      const auto kind = row.at("Kind").get<std::string>();
      const auto civilization_id = row.at("CivilizationId").get<int>();
      const auto capability_id = row.at("CapabilityId").get<std::string>();
      auto state = campaign(runtime, row);
      const auto before_state = state_view(state);
      require(row.at("BeforeState") == row.at("AfterState"),
              name + " source query mutated state.");
      const auto before = fingerprint(research_root);
      std::optional<bool> result;
      std::optional<Error> error;

      if (kind == "construction") {
        const AdaptiveResearchConstructionCapabilityView view(state);
        error = invoke([&] {
          result = view.has_civilization_capability(civilization_id,
                                                     capability_id);
        });
      } else if (kind == "shipbuilding") {
        const AdaptiveResearchShipbuildingCapabilityView view(state);
        error = invoke([&] {
          result = view.has_civilization_capability(civilization_id,
                                                     capability_id);
        });
      } else {
        throw std::runtime_error(name + " has unknown row kind.");
      }

      const auto after = fingerprint(research_root);
      require(before == row.at("BeforeFingerprint").get<std::string>() &&
                  after == row.at("AfterFingerprint").get<std::string>(),
              name + " input fingerprint differed.");
      require(before_state == state_view(state), name + " mutated state.");
      compare_error(row.at("Error"), error, name);
      if (row.at("Result").is_null())
        require(!result, name + " unexpectedly returned a result.");
      else
        require(result && *result == row.at("Result").get<bool>(),
                name + " result differed.");
      ++count;
    }
    require(count == fixture.at("RowCount").get<std::size_t>(),
            "Retained row count differed.");
    require(fingerprint(research_root) ==
                fixture.at("FinalFingerprint").get<std::string>(),
            "Canonical input fingerprint differed after replay.");
    std::cout << "Adaptive Research capability adapter parity passed " << count
              << " actual-source rows.\n";
    return 0;
  } catch (const std::exception &error) {
    std::cerr << error.what() << '\n';
    std::cerr << "cwd=" << std::filesystem::current_path().string() << '\n';
    std::cerr << "fixture=" << (argc > 1 ? argv[1] : "<missing>") << '\n';
    std::cerr << "researchRoot=" << (argc > 2 ? argv[2] : "<missing>")
              << '\n';
    return 1;
  }
}
