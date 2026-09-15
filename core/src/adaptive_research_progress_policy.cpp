#include <stellar/core/adaptive_research_progress_policy.hpp>

#include <nlohmann/json.hpp>

#include <algorithm>
#include <charconv>
#include <cmath>
#include <fstream>
#include <unordered_map>

namespace stellar::core {
namespace {
using Json = nlohmann::ordered_json;

[[noreturn]] void fail(std::string message) { throw AdaptiveResearchProgressPolicyError(std::move(message)); }
std::string maturity_name(ResearchMaturity stage) { switch(stage) { case ResearchMaturity::rumored: return "Rumored"; case ResearchMaturity::hypothesized: return "Hypothesized"; case ResearchMaturity::investigable: return "Investigable"; case ResearchMaturity::experimental: return "Experimental"; case ResearchMaturity::demonstrated: return "Demonstrated"; case ResearchMaturity::engineering: return "Engineering"; case ResearchMaturity::mature: return "Mature"; case ResearchMaturity::archived: return "Archived"; default: return std::to_string(static_cast<int>(stage)); } }
std::string dotnet_double(double value) { char buffer[64]; const auto result=std::to_chars(buffer,buffer+sizeof(buffer),value); std::string text(buffer,result.ptr); const auto exponent=text.find('e'); if(exponent!=std::string::npos) { text[exponent]='E'; auto digits=exponent+1; if(digits<text.size() && (text[digits]=='+' || text[digits]=='-')) ++digits; if(text.size()-digits==1) text.insert(digits,"0"); } return text; }
Json read_json(const std::filesystem::path &path) {
  std::ifstream input(path);
  if (!input) fail("Unable to read Adaptive Research file: " + path.string());
  try { return Json::parse(input); }
  catch (const Json::exception &) { fail("Malformed Adaptive Research JSON in " + path.string()); }
}
const Json &property(const Json &value, std::string_view name, const std::string &source) {
  const auto found = value.find(name);
  if (found == value.end()) fail(source + " is missing property '" + std::string(name) + "'.");
  return *found;
}
double number(const Json &value, std::string_view name, const std::string &source) {
  const auto &item = property(value, name, source);
  if (!item.is_number()) fail(source + "." + std::string(name) + " must be numeric.");
  return item.get<double>();
}
void catalog_id(const Json &root, const AdaptiveResearchCatalog &catalog, const std::string &name) {
  const auto &item = property(root, "catalog_id", name);
  if (!item.is_string() || item.get<std::string>() != catalog.metadata().catalog_id)
    fail(name + " catalog_id '" + (item.is_string() ? item.get<std::string>() : std::string()) + "' does not match '" + catalog.metadata().catalog_id + "'.");
}
ResearchStageWorkBand stage(const Json &stages, std::string_view name, ResearchMaturity maturity, const std::string &source) {
  const auto &item = property(stages, name, source);
  return {maturity, number(item, "typical_rp_fraction_start", source), number(item, "typical_rp_fraction_end", source)};
}
void validate_stages(std::vector<ResearchStageWorkBand> values) {
  std::ranges::stable_sort(values, {}, &ResearchStageWorkBand::start_fraction);
  if (values.size() != 3) fail("Expected exactly three directed research work bands.");
  double expected = 0.;
  for (const auto &band : values) {
    if (std::abs(band.start_fraction - expected) > .000001 || band.end_fraction <= band.start_fraction || band.end_fraction > 1. + .000001)
      fail("Invalid or non-contiguous research stage band " + maturity_name(band.stage) + ": " + dotnet_double(band.start_fraction) + ".." + dotnet_double(band.end_fraction) + ".");
    expected = band.end_fraction;
  }
  if (std::abs(expected - 1.) > .000001) fail("Directed research stage work bands do not cover total project work 0..1.");
}
void validate_readiness(const std::vector<ResearchReadinessEfficiencyBand> &bands) {
  if (bands.empty()) fail("No readiness-to-progress bands are configured.");
  if (std::abs(bands.front().minimum_score) > .000001) fail("Readiness bands must begin at score 0.");
  double previous = -1.;
  for (size_t i = 0; i < bands.size(); ++i) {
    const auto &band = bands[i];
    if (band.minimum_score < 0. || band.minimum_score > 100. || band.maximum_score < band.minimum_score || band.maximum_score > 100. || band.minimum_score <= previous || band.efficiency <= 0. || std::isnan(band.efficiency) || std::isinf(band.efficiency))
      fail("Invalid readiness band " + dotnet_double(band.minimum_score) + ".." + dotnet_double(band.maximum_score) + ".");
    if (i + 1 < bands.size() && (bands[i + 1].minimum_score <= band.minimum_score || bands[i + 1].minimum_score > band.maximum_score + 1.000001))
      fail("Readiness band transition " + dotnet_double(band.minimum_score) + ".." + dotnet_double(band.maximum_score) + " -> " + dotnet_double(bands[i + 1].minimum_score) + " creates an invalid continuous threshold sequence.");
    previous = band.minimum_score;
  }
  if (bands.back().maximum_score < 100. - .000001) fail("Readiness bands must cover score 100.");
}
} // namespace

struct AdaptiveResearchProgressPolicy::Storage { std::vector<ResearchStageWorkBand> stages; std::vector<ResearchReadinessEfficiencyBand> readiness; };
AdaptiveResearchProgressPolicyError::AdaptiveResearchProgressPolicyError(std::string m) : std::runtime_error(std::move(m)) {}
AdaptiveResearchProgressPolicy::AdaptiveResearchProgressPolicy(Storage s) : storage_(std::make_unique<Storage>(std::move(s))) {}
AdaptiveResearchProgressPolicy::AdaptiveResearchProgressPolicy(AdaptiveResearchProgressPolicy &&) noexcept = default;
AdaptiveResearchProgressPolicy &AdaptiveResearchProgressPolicy::operator=(AdaptiveResearchProgressPolicy &&) noexcept = default;
AdaptiveResearchProgressPolicy::~AdaptiveResearchProgressPolicy() = default;
std::span<const ResearchStageWorkBand> AdaptiveResearchProgressPolicy::stage_bands() const noexcept { return storage_->stages; }
std::span<const ResearchReadinessEfficiencyBand> AdaptiveResearchProgressPolicy::readiness_bands() const noexcept { return storage_->readiness; }
const ResearchStageWorkBand &AdaptiveResearchProgressPolicy::get_stage_band(ResearchMaturity stage_value) const {
  const auto found = std::ranges::find(storage_->stages, stage_value, &ResearchStageWorkBand::stage);
  if (found == storage_->stages.end()) fail("Research maturity '" + maturity_name(stage_value) + "' is not a directed-work stage.");
  return *found;
}
double AdaptiveResearchProgressPolicy::get_stage_work(const AdaptiveResearchNodeDefinition &node, ResearchMaturity stage_value) const { return node.project_requirements.base_research_points * get_stage_band(stage_value).work_fraction(); }
double AdaptiveResearchProgressPolicy::get_readiness_efficiency(double score) const {
  score = std::clamp(score, 0., 100.); // std::clamp preserves NaN, matching Math.Clamp.
  const ResearchReadinessEfficiencyBand *selected = nullptr;
  for (const auto &band : storage_->readiness) { if (score + .000001 < band.minimum_score) break; selected = &band; }
  if (!selected) fail("No readiness efficiency band covers score " + dotnet_double(score) + ".");
  return selected->efficiency;
}
AdaptiveResearchProgressPolicy load_adaptive_research_progress_policy(const std::filesystem::path &root, const AdaptiveResearchCatalog &catalog) {
  const auto maturation = read_json(root / "maturation_model.json"); catalog_id(maturation, catalog, "maturation_model.json");
  const auto &directed = property(maturation, "directed_project_stages", "maturation_model.json");
  AdaptiveResearchProgressPolicy::Storage storage;
  storage.stages = {stage(directed, "experimental", ResearchMaturity::experimental, "maturation_model.json"), stage(directed, "demonstrated", ResearchMaturity::demonstrated, "maturation_model.json"), stage(directed, "engineering", ResearchMaturity::engineering, "maturation_model.json")};
  validate_stages(storage.stages);
  const auto readiness = read_json(root / "project_readiness_model.json"); catalog_id(readiness, catalog, "project_readiness_model.json");
  const auto &items = property(readiness, "readiness_to_progress_efficiency", "project_readiness_model.json");
  if (!items.is_array()) fail("project_readiness_model.json.readiness_to_progress_efficiency must be an array.");
  for (const auto &item : items) storage.readiness.push_back({number(item, "min", "project_readiness_model.json"), number(item, "max", "project_readiness_model.json"), number(item, "efficiency", "project_readiness_model.json")});
  std::ranges::stable_sort(storage.readiness, {}, &ResearchReadinessEfficiencyBand::minimum_score); validate_readiness(storage.readiness);
  return AdaptiveResearchProgressPolicy(std::move(storage));
}
} // namespace stellar::core
