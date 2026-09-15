#include <stellar/core/adaptive_research_agenda.hpp>
#include <stellar/core/detail/adaptive_research_agenda_support_access.hpp>

#include <stellar/core/detail/adaptive_research_weak_state_table.hpp>
#include <stellar/core/detail/legacy_number_format.hpp>

#include <nlohmann/json.hpp>

#include <algorithm>
#include <cmath>
#include <fstream>
#include <limits>
#include <map>
#include <optional>
#include <set>
#include <unordered_map>
#include <utility>

namespace stellar::core {
namespace {

using Json = nlohmann::json;

[[noreturn]] void catalog_fail(std::string message) {
  throw AdaptiveResearchAgendaCatalogError(std::move(message));
}

std::string bytes(const std::filesystem::path &path) {
  std::ifstream input(path, std::ios::binary);
  if (!input) catalog_fail("Could not find file '" + path.string() + "'.");
  return {std::istreambuf_iterator<char>(input), {}};
}

Json read_json(const std::filesystem::path &path) {
  try {
    return Json::parse(bytes(path));
  } catch (const AdaptiveResearchAgendaCatalogError &) {
    throw;
  } catch (const Json::exception &error) {
    catalog_fail(error.what());
  }
}

bool consume_dotnet_whitespace(std::string_view &value) {
  const auto first = static_cast<unsigned char>(value.front());
  std::uint32_t code_point = first;
  std::size_t length = 1;
  if ((first & 0xe0) == 0xc0) {
    code_point = first & 0x1f;
    length = 2;
  } else if ((first & 0xf0) == 0xe0) {
    code_point = first & 0x0f;
    length = 3;
  } else if ((first & 0xf8) == 0xf0) {
    code_point = first & 0x07;
    length = 4;
  } else if (first >= 0x80) {
    return false;
  }
  if (value.size() < length) return false;
  for (std::size_t index = 1; index < length; ++index) {
    const auto continuation = static_cast<unsigned char>(value[index]);
    if ((continuation & 0xc0) != 0x80) return false;
    code_point = (code_point << 6) | (continuation & 0x3f);
  }
  value.remove_prefix(length);
  return (code_point >= 0x09 && code_point <= 0x0d) || code_point == 0x20 ||
         code_point == 0x85 || code_point == 0xa0 || code_point == 0x1680 ||
         (code_point >= 0x2000 && code_point <= 0x200a) ||
         code_point == 0x2028 || code_point == 0x2029 ||
         code_point == 0x202f || code_point == 0x205f ||
         code_point == 0x3000;
}

bool blank(std::string_view value) {
  if (value.empty()) return true;
  while (!value.empty())
    if (!consume_dotnet_whitespace(value)) return false;
  return true;
}

std::string required_string(const Json &object, const char *name,
                            std::string_view source) {
  const auto found = object.find(name);
  if (found == object.end() || !found->is_string() ||
      blank(found->get_ref<const std::string &>()))
    catalog_fail(std::string(source) + " is missing non-empty string '" +
                 name + "'.");
  return found->get<std::string>();
}

void validate_catalog_id(const Json &root, std::string_view expected,
                         std::string_view source) {
  const auto actual = required_string(root, "catalog_id", source);
  if (actual != expected)
    catalog_fail(std::string(source) + " catalog_id '" + actual +
                 "' does not match '" + std::string(expected) + "'.");
}

int int32(const Json &value) {
  if (!value.is_number_integer() && !value.is_number_unsigned())
    throw Json::type_error::create(302, "type must be number", &value);
  if (value.is_number_unsigned()) {
    const auto result = value.get<std::uint64_t>();
    if (result > static_cast<std::uint64_t>(std::numeric_limits<int>::max()))
      throw Json::out_of_range::create(406, "number overflow", &value);
    return static_cast<int>(result);
  }
  const auto result = value.get<std::int64_t>();
  if (result < std::numeric_limits<int>::min() ||
      result > std::numeric_limits<int>::max())
    throw Json::out_of_range::create(406, "number overflow", &value);
  return static_cast<int>(result);
}

struct TransparentHash {
  using is_transparent = void;
  std::size_t operator()(std::string_view value) const noexcept {
    return std::hash<std::string_view>{}(value);
  }
  std::size_t operator()(const std::string &value) const noexcept {
    return (*this)(std::string_view(value));
  }
};
struct TransparentEqual {
  using is_transparent = void;
  bool operator()(std::string_view left,
                  std::string_view right) const noexcept {
    return left == right;
  }
};

class PrioritySlots final {
  struct Entry {
    ResearchAgendaPriorityEntry value;
    bool active{true};
  };
  std::vector<Entry> entries_;
  std::vector<std::size_t> free_;
  std::unordered_map<std::string, std::size_t, TransparentHash,
                     TransparentEqual> index_;
  std::vector<ResearchAgendaPriorityEntry> cache_;

  void rebuild() {
    cache_.clear();
    for (const auto &entry : entries_)
      if (entry.active)
        cache_.push_back(entry.value);
  }

public:
  [[nodiscard]] const ResearchAgendaPriorityEntry *
  find(std::string_view key) const noexcept {
    const auto found=index_.find(key);
    return found==index_.end()?nullptr:&entries_[found->second].value;
  }
  [[nodiscard]] std::span<const ResearchAgendaPriorityEntry>
  values() const noexcept { return cache_; }
  bool set(std::string key,std::string priority,std::string_view default_id) {
    const auto found=index_.find(key);
    if(priority==default_id) {
      if(found==index_.end()) return false;
      entries_[found->second].active=false;
      free_.push_back(found->second);
      index_.erase(found);
      rebuild();
      return true;
    }
    if(found!=index_.end()) {
      auto &value=entries_[found->second].value.priority_id;
      if(value==priority) return false;
      value=std::move(priority);
      rebuild();
      return true;
    }
    std::size_t position;
    if(free_.empty()) {
      position=entries_.size();
      entries_.push_back({{std::move(key),std::move(priority)},true});
    } else {
      position=free_.back();
      free_.pop_back();
      entries_[position]={{std::move(key),std::move(priority)},true};
    }
    index_.emplace(entries_[position].value.key,position);
    rebuild();
    return true;
  }
};

std::int64_t increment(std::int64_t value) {
  if (value == std::numeric_limits<std::int64_t>::max())
    throw std::overflow_error("Adaptive Research agenda revision overflow.");
  return value + 1;
}

double validate100(double value, std::string_view name) {
  if (value < 0 || value > 100 || !std::isfinite(value)) {
    throw std::out_of_range("Value must be in 0..100. (Parameter '" +
                            std::string(name) + "')\r\nActual value was " +
                            detail::legacy_general(value) + ".");
  }
  return value;
}

} // namespace

struct AdaptiveResearchAgendaCatalog::Storage {
  std::vector<ResearchAgendaPriorityDefinition> priorities;
  std::vector<ResearchScientificCultureAxisDefinition> culture_axes;
  std::vector<std::string> utility_component_ids;
  int shortlist_bound{};
  ResearchAgendaRuntimePolicy policy;
};

AdaptiveResearchAgendaCatalogError::AdaptiveResearchAgendaCatalogError(
    std::string message)
    : std::runtime_error(std::move(message)) {}

AdaptiveResearchAgendaCatalog::AdaptiveResearchAgendaCatalog(
    std::unique_ptr<Storage> storage) noexcept
    : storage_(std::move(storage)) {}
AdaptiveResearchAgendaCatalog::~AdaptiveResearchAgendaCatalog() = default;
AdaptiveResearchAgendaCatalog::AdaptiveResearchAgendaCatalog(
    AdaptiveResearchAgendaCatalog &&) noexcept = default;
AdaptiveResearchAgendaCatalog &AdaptiveResearchAgendaCatalog::operator=(
    AdaptiveResearchAgendaCatalog &&) noexcept = default;
std::span<const ResearchAgendaPriorityDefinition>
AdaptiveResearchAgendaCatalog::priorities() const noexcept {
  return storage_->priorities;
}
std::span<const ResearchScientificCultureAxisDefinition>
AdaptiveResearchAgendaCatalog::culture_axes() const noexcept {
  return storage_->culture_axes;
}
std::span<const std::string>
AdaptiveResearchAgendaCatalog::utility_component_ids() const noexcept {
  return storage_->utility_component_ids;
}
int AdaptiveResearchAgendaCatalog::shortlist_bound() const noexcept {
  return storage_->shortlist_bound;
}
const ResearchAgendaRuntimePolicy &
AdaptiveResearchAgendaCatalog::runtime_policy() const noexcept {
  return storage_->policy;
}
const ResearchAgendaPriorityDefinition &
AdaptiveResearchAgendaCatalog::get_priority(std::string_view id) const {
  const auto found = std::ranges::find(storage_->priorities, id,
                                       &ResearchAgendaPriorityDefinition::id);
  if (found == storage_->priorities.end())
    throw std::out_of_range("Unknown research agenda priority '" +
                            std::string(id) + "'.");
  return *found;
}

AdaptiveResearchAgendaCatalog load_adaptive_research_agenda_catalog(
    const std::filesystem::path &root_path,
    const AdaptiveResearchCatalog &catalog,
    const AdaptiveResearchExpertiseCatalog &expertise_catalog) {
  const auto root = std::filesystem::absolute(root_path);
  auto storage = std::make_unique<AdaptiveResearchAgendaCatalog::Storage>();
  try {
    const auto agenda = read_json(root / "research_agenda_model.json");
    validate_catalog_id(agenda, catalog.metadata().catalog_id,
                        "research_agenda_model.json");
    const auto &priority_levels=agenda.at("priority_levels");
    if(!priority_levels.is_array())
      throw Json::type_error::create(302,"priority_levels must be an array",&priority_levels);
    for (const auto &element : priority_levels) {
      auto id = required_string(element, "id", "research_agenda_model.json");
      const auto rank=int32(element.at("rank"));
      if (std::ranges::any_of(storage->priorities,
                             [&](const auto &value) { return value.id == id; }))
        catalog_fail("Duplicate research agenda priority '" + id + "'.");
      storage->priorities.push_back({std::move(id), rank, 0});
    }
    std::vector<int> ranks;
    for (const auto &value : storage->priorities) ranks.push_back(value.rank);
    std::ranges::sort(ranks);
    if (storage->priorities.size() != 5 ||
        ranks != std::vector<int>({0, 1, 2, 3, 4}))
      catalog_fail("Research agenda priority levels must contain ranks 0..4 exactly once.");

    const auto culture = read_json(root / "scientific_culture_model.json");
    validate_catalog_id(culture, catalog.metadata().catalog_id,
                        "scientific_culture_model.json");
    const auto &culture_axes=culture.at("axes");
    if(!culture_axes.is_array())
      throw Json::type_error::create(302,"axes must be an array",&culture_axes);
    for (const auto &element : culture_axes) {
      const auto id = required_string(element, "id", "scientific_culture_model.json");
      const auto &range = element.at("range");
      if (!range.is_array() || range.size() != 2 ||
          std::abs(range[0].get<double>()) > 0.000001 ||
          std::abs(range[1].get<double>() - 100) > 0.000001)
        catalog_fail("Scientific culture axis '" + id +
                     "' must use range 0..100.");
      auto name=required_string(element, "name", id);
      if (std::ranges::any_of(storage->culture_axes,
                             [&](const auto &value) { return value.id == id; }))
        throw std::invalid_argument(
            "An item with the same key has already been added. Key: " + id);
      storage->culture_axes.push_back(
          {id, std::move(name)});
    }
    if (storage->culture_axes.size() != 12)
      catalog_fail("Expected 12 scientific-culture axes, found " +
                   std::to_string(storage->culture_axes.size()) + ".");

    const auto ai = read_json(root / "research_ai_planning_contract.json");
    validate_catalog_id(ai, catalog.metadata().catalog_id,
                        "research_ai_planning_contract.json");
    const auto &utility_components=ai.at("visible_candidate_utility_components");
    if(!utility_components.is_array())
      throw Json::type_error::create(302,"visible_candidate_utility_components must be an array",&utility_components);
    for (const auto &element : utility_components)
      storage->utility_component_ids.push_back(required_string(
          element, "id", "research_ai_planning_contract.json"));
    storage->shortlist_bound = int32(
        ai.at("shortlist_policy").at("bounded_candidate_count"));
    if (storage->shortlist_bound <= 0 || storage->shortlist_bound > 64)
      catalog_fail("Invalid visible research shortlist bound " +
                   std::to_string(storage->shortlist_bound) + ".");

    const auto policy = read_json(root / "research_agenda_runtime_policy.json");
    validate_catalog_id(policy, catalog.metadata().catalog_id,
                        "research_agenda_runtime_policy.json");
    const auto &defaults = policy.at("defaults");
    const auto &scores = policy.at("priority_scores");
    if(!scores.is_object())
      throw Json::type_error::create(302,"priority_scores must be an object",&scores);
    for (auto &priority : storage->priorities) {
      const auto found = scores.find(priority.id);
      if (found == scores.end())
        catalog_fail("Agenda runtime policy is missing score for priority '" +
                     priority.id + "'.");
      priority.score = found->get<double>();
    }
    const auto &ranking = policy.at("visible_candidate_ranking");
    const auto &adequacy = policy.at("perceived_adequacy_review");
    storage->policy = {
        required_string(defaults, "priority_level",
                        "research_agenda_runtime_policy.json"),
        defaults.at("basic_vs_applied_orientation").get<double>(),
        defaults.at("competence_preservation_policy").get<double>(),
        defaults.at("portfolio_diversity_policy").get<double>(),
        defaults.at("foreign_science_engagement").get<double>(),
        defaults.at("scientific_culture_axis").get<double>(),
        ranking.at("blocked_project_score_multiplier").get<double>(),
        ranking.at("already_covered_solution_value").get<double>(),
        ranking.at("novel_solution_value").get<double>(),
        ranking.at("time_to_effect_half_value_years").get<double>(),
        ranking.at("lab_cost_half_value_fraction_of_total_capacity").get<double>(),
        adequacy.at("minimum_confidence_for_policy_change").get<double>(),
        adequacy.at("low_relevant_pressure_max").get<double>(),
        adequacy.at("high_adequacy_min").get<double>(),
        adequacy.at("deprioritize_threshold").get<double>(),
        adequacy.at("important_challenge_threshold").get<double>(),
        adequacy.at("strategic_challenge_threshold").get<double>(),
        adequacy.at("critical_challenge_threshold").get<double>()};
    if (std::ranges::none_of(storage->priorities, [&](const auto &value) {
          return value.id == storage->policy.default_priority_id;
        }))
      catalog_fail("Unknown default agenda priority '" +
                   storage->policy.default_priority_id + "'.");
    const double default_values[] = {
        storage->policy.default_basic_vs_applied_orientation,
        storage->policy.default_competence_preservation,
        storage->policy.default_portfolio_diversity,
        storage->policy.default_foreign_science_engagement,
        storage->policy.default_culture_axis};
    for (const auto value : default_values)
      if (value < 0 || value > 100)
        catalog_fail("Research agenda defaults must remain in 0..100.");

    std::set<std::string, std::less<>> domains;
    for (const auto &node : catalog.nodes()) domains.insert(node.domain_id);
    if (domains.size() != static_cast<std::size_t>(catalog.metadata().domain_count))
      catalog_fail("Agenda domain discovery does not match catalog metadata.");
    if (expertise_catalog.fields().size() !=
        static_cast<std::size_t>(catalog.metadata().knowledge_field_count))
      catalog_fail("Agenda knowledge-field catalog mismatch.");
  } catch (const AdaptiveResearchAgendaCatalogError &) {
    throw;
  } catch (const Json::exception &error) {
    throw AdaptiveResearchAgendaCatalogError(error.what());
  }
  return AdaptiveResearchAgendaCatalog(std::move(storage));
}

struct AdaptiveResearchAgendaState::Storage {
  std::int64_t revision{};
  ResearchAgendaOrientationState orientations;
  double last_major_review_year = -std::numeric_limits<double>::infinity();
  std::string policy_provenance = "default";
  PrioritySlots domain_priorities;
  PrioritySlots field_priorities;
  PrioritySlots problem_priorities;
  PrioritySlots capability_priorities;
  std::vector<ResearchScientificCultureAxisState> culture_axes;
};

AdaptiveResearchAgendaState::AdaptiveResearchAgendaState(
    const AdaptiveResearchAgendaCatalog &catalog)
    : storage_(std::make_unique<Storage>()) {
  const auto &defaults = catalog.runtime_policy();
  storage_->orientations = {defaults.default_basic_vs_applied_orientation,
                            defaults.default_competence_preservation,
                            defaults.default_portfolio_diversity,
                            defaults.default_foreign_science_engagement};
  for (const auto &axis : catalog.culture_axes())
    storage_->culture_axes.push_back({axis.id, defaults.default_culture_axis});
}
AdaptiveResearchAgendaState::~AdaptiveResearchAgendaState() = default;
AdaptiveResearchAgendaState::AdaptiveResearchAgendaState(
    AdaptiveResearchAgendaState &&) noexcept = default;
AdaptiveResearchAgendaState &AdaptiveResearchAgendaState::operator=(
    AdaptiveResearchAgendaState &&) noexcept = default;
std::int64_t AdaptiveResearchAgendaState::revision() const noexcept { return storage_->revision; }
const ResearchAgendaOrientationState &AdaptiveResearchAgendaState::orientations() const noexcept { return storage_->orientations; }
double AdaptiveResearchAgendaState::last_major_review_year() const noexcept { return storage_->last_major_review_year; }
const std::string &AdaptiveResearchAgendaState::policy_provenance() const noexcept { return storage_->policy_provenance; }
std::span<const ResearchAgendaPriorityEntry> AdaptiveResearchAgendaState::domain_priorities() const noexcept { return storage_->domain_priorities.values(); }
std::span<const ResearchAgendaPriorityEntry> AdaptiveResearchAgendaState::field_priorities() const noexcept { return storage_->field_priorities.values(); }
std::span<const ResearchAgendaPriorityEntry> AdaptiveResearchAgendaState::problem_priorities() const noexcept { return storage_->problem_priorities.values(); }
std::span<const ResearchAgendaPriorityEntry> AdaptiveResearchAgendaState::capability_priorities() const noexcept { return storage_->capability_priorities.values(); }
std::span<const ResearchScientificCultureAxisState> AdaptiveResearchAgendaState::culture_axes() const noexcept { return storage_->culture_axes; }
std::string_view AdaptiveResearchAgendaState::get_domain_priority(std::string_view id, std::string_view fallback) const noexcept { if (const auto *v=storage_->domain_priorities.find(id)) return v->priority_id; return fallback; }
std::string_view AdaptiveResearchAgendaState::get_field_priority(std::string_view id, std::string_view fallback) const noexcept { if (const auto *v=storage_->field_priorities.find(id)) return v->priority_id; return fallback; }
std::string_view AdaptiveResearchAgendaState::get_problem_priority(std::string_view id, std::string_view fallback) const noexcept { if (const auto *v=storage_->problem_priorities.find(id)) return v->priority_id; return fallback; }
std::string_view AdaptiveResearchAgendaState::get_capability_priority(std::string_view id, std::string_view fallback) const noexcept { if (const auto *v=storage_->capability_priorities.find(id)) return v->priority_id; return fallback; }
double AdaptiveResearchAgendaState::get_culture_axis(std::string_view id) const noexcept { const auto found=std::ranges::find(storage_->culture_axes,id,&ResearchScientificCultureAxisState::axis_id); return found==storage_->culture_axes.end()?50.0:found->value; }

namespace {
bool set_priority(PrioritySlots &entries,
                  std::int64_t &revision, std::string key,
                  std::string priority_id, std::string_view default_id) {
  if(!entries.set(std::move(key),std::move(priority_id),default_id)) return false;
  revision = increment(revision);
  return true;
}
} // namespace

bool detail::AdaptiveResearchAgendaStateWriter::set_domain_priority(
    AdaptiveResearchAgendaState &state, std::string key,
    std::string priority_id, std::string_view default_id) {
  return set_priority(state.storage_->domain_priorities, state.storage_->revision,
                      std::move(key), std::move(priority_id), default_id);
}
bool detail::AdaptiveResearchAgendaStateWriter::set_field_priority(
    AdaptiveResearchAgendaState &state, std::string key,
    std::string priority_id, std::string_view default_id) {
  return set_priority(state.storage_->field_priorities, state.storage_->revision,
                      std::move(key), std::move(priority_id), default_id);
}
bool detail::AdaptiveResearchAgendaStateWriter::set_problem_priority(
    AdaptiveResearchAgendaState &state, std::string key,
    std::string priority_id, std::string_view default_id) {
  return set_priority(state.storage_->problem_priorities, state.storage_->revision,
                      std::move(key), std::move(priority_id), default_id);
}
bool detail::AdaptiveResearchAgendaStateWriter::set_capability_priority(
    AdaptiveResearchAgendaState &state, std::string key,
    std::string priority_id, std::string_view default_id) {
  return set_priority(state.storage_->capability_priorities,
                      state.storage_->revision, std::move(key),
                      std::move(priority_id), default_id);
}
bool detail::AdaptiveResearchAgendaStateWriter::set_culture_axis(
    AdaptiveResearchAgendaState &state, std::string axis_id, double value) {
  value = validate100(value, "value");
  const auto found = std::ranges::find(state.storage_->culture_axes, axis_id,
                                       &ResearchScientificCultureAxisState::axis_id);
  if (found != state.storage_->culture_axes.end() &&
      std::abs(found->value - value) < 0.000001)
    return false;
  if (found == state.storage_->culture_axes.end())
    state.storage_->culture_axes.push_back({std::move(axis_id), value});
  else
    found->value = value;
  state.storage_->revision = increment(state.storage_->revision);
  return true;
}
void detail::AdaptiveResearchAgendaStateWriter::set_orientations(
    AdaptiveResearchAgendaState &state, ResearchAgendaOrientationState value) {
  value = {validate100(value.basic_vs_applied_orientation,
                       "BasicVsAppliedOrientation"),
           validate100(value.competence_preservation_policy,
                       "CompetencePreservationPolicy"),
           validate100(value.portfolio_diversity_policy,
                       "PortfolioDiversityPolicy"),
           validate100(value.foreign_science_engagement,
                       "ForeignScienceEngagement")};
  if (state.storage_->orientations == value) return;
  state.storage_->orientations = value;
  state.storage_->revision = increment(state.storage_->revision);
}
void detail::AdaptiveResearchAgendaStateWriter::mark_reviewed(
    AdaptiveResearchAgendaState &state, double year, std::string provenance) {
  if (!std::isfinite(year))
    throw std::out_of_range("Specified argument was out of the range of valid values. (Parameter 'currentYear')");
  state.storage_->last_major_review_year = year;
  state.storage_->policy_provenance = blank(provenance) ? "unspecified" : std::move(provenance);
  state.storage_->revision = increment(state.storage_->revision);
}

void detail::AdaptiveResearchAgendaSupportAccess::set_review_metadata_unchecked(
    AdaptiveResearchAgendaState &state, double year, std::string provenance) {
  state.storage_->last_major_review_year = year;
  state.storage_->policy_provenance = std::move(provenance);
}

namespace {

using AgendaWriter = detail::AdaptiveResearchAgendaStateWriter;

struct AgendaStateHolder {
  std::unique_ptr<AdaptiveResearchAgendaState> value;
};

struct AgendaCandidateCache {
  bool valid{};
  std::int64_t core_revision{};
  std::int64_t expertise_revision{};
  std::int64_t agenda_revision{};
  std::uint64_t rebuild_count{};
  std::vector<ResearchVisibleProjectCandidate> candidates;
};

bool contains(std::span<const std::string> values, std::string_view value) {
  return std::ranges::find(values, value) != values.end();
}

bool has_project(const AdaptiveResearchCivilizationState &state,
                 std::string_view node_id) {
  return std::ranges::find(state.active_projects(), node_id,
                           &ResearchProjectRuntimeState::node_id) !=
         state.active_projects().end();
}

bool double_descending(double left, double right) {
  if (std::isnan(left)) return false;
  if (std::isnan(right)) return true;
  return left > right;
}

std::vector<std::uint16_t> utf16(std::string_view value) {
  std::vector<std::uint16_t> result;
  while (!value.empty()) {
    const auto first = static_cast<unsigned char>(value.front());
    std::uint32_t point = first;
    std::size_t length = 1;
    if ((first & 0xe0) == 0xc0) { point = first & 0x1f; length = 2; }
    else if ((first & 0xf0) == 0xe0) { point = first & 0x0f; length = 3; }
    else if ((first & 0xf8) == 0xf0) { point = first & 7; length = 4; }
    if (value.size() < length) break;
    for (std::size_t index = 1; index < length; ++index)
      point = (point << 6) |
              (static_cast<unsigned char>(value[index]) & 0x3f);
    value.remove_prefix(length);
    if (point <= 0xffff) result.push_back(static_cast<std::uint16_t>(point));
    else {
      point -= 0x10000;
      result.push_back(static_cast<std::uint16_t>(0xd800 + (point >> 10)));
      result.push_back(static_cast<std::uint16_t>(0xdc00 + (point & 0x3ff)));
    }
  }
  return result;
}

bool ordinal_less(std::string_view left, std::string_view right) {
  return utf16(left) < utf16(right);
}

std::string one_decimal(double value) {
  return detail::legacy_custom_fixed(value, 0, 1);
}

} // namespace

struct AdaptiveResearchAgendaRuntime::Storage {
  const AdaptiveResearchAuthority *authority;
  const AdaptiveResearchAgendaCatalog *catalog;
  mutable detail::AdaptiveResearchWeakStateTable<AgendaStateHolder> states;
  mutable detail::AdaptiveResearchWeakStateTable<AgendaCandidateCache> caches;

  Storage(const AdaptiveResearchAuthority &authority_value,
          const AdaptiveResearchAgendaCatalog &catalog_value)
      : authority(&authority_value), catalog(&catalog_value) {}

  AdaptiveResearchAgendaState &agenda(
      const AdaptiveResearchCivilizationState &civilization) const {
    auto &holder = states.get_or_create(civilization, [&] {
      return AgendaStateHolder{
          std::unique_ptr<AdaptiveResearchAgendaState>(
              new AdaptiveResearchAgendaState(*catalog))};
    });
    return *holder.value;
  }

  void invalidate(const AdaptiveResearchCivilizationState &civilization) const {
    if (auto *cache = caches.try_get(civilization)) cache->valid = false;
  }

  double priority_score(std::string_view priority_id) const {
    return catalog->get_priority(priority_id).score;
  }

  void set_priority(const AdaptiveResearchCivilizationState &civilization,
                    PrioritySlots AdaptiveResearchAgendaState::Storage::*member,
                    std::string key, std::string priority_id) const {
    auto &value = agenda(civilization);
    (void)catalog->get_priority(priority_id);
    ::stellar::core::set_priority(value.storage_.get()->*member,
                                  value.storage_->revision, std::move(key),
                                  std::move(priority_id),
                                  catalog->runtime_policy().default_priority_id);
    invalidate(civilization);
  }

  double visible_domain_pressure(
      const AdaptiveResearchCivilizationState &civilization,
      std::string_view domain_id) const {
    std::vector<std::string> pressure_ids;
    auto add = [&](std::string_view id) {
      if (!contains(pressure_ids, id)) pressure_ids.emplace_back(id);
    };
    for (const auto &node_state : civilization.node_states()) {
      const auto &node = authority->catalog().get_node(node_state.node_id);
      if (node.domain_id != domain_id) continue;
      for (const auto &id : node.pressure_affinities) add(id);
      for (const auto &item : node.project_requirements.required_pressure)
        add(item.id);
      for (const auto &item : node.project_requirements.required_pressure_any)
        add(item.id);
    }
    double result{};
    for (const auto &id : pressure_ids)
      result = std::max(result, civilization.get_pressure(id));
    return result;
  }

  ResearchVisibleProjectCandidate score(
      const AdaptiveResearchCivilizationState &civilization,
      const AdaptiveResearchAgendaState &agenda_state,
      std::string_view node_id) const {
    const auto &node = authority->catalog().get_node(node_id);
    const auto free_labs = civilization.free_effective_labs();
    const double requested_labs = free_labs <= 0
        ? node.project_requirements.minimum_labs
        : std::max<double>(node.project_requirements.minimum_labs,
                   std::min<double>(node.project_requirements.recommended_labs,
                                    free_labs));
    const auto readiness = authority->get_project_readiness(
        civilization, node.id, ResearchMaturity::experimental, requested_labs);
    const auto eligibility = authority->kernel().eligibility().evaluate_project_start(
        civilization, node.id, requested_labs);
    std::vector<ResearchProjectUtilityComponent> components;
    std::vector<std::string> pressure_ids;
    auto add_pressure = [&](std::string_view id) {
      if (!contains(pressure_ids, id)) pressure_ids.emplace_back(id);
    };
    for (const auto &id : node.pressure_affinities) add_pressure(id);
    for (const auto &item : node.project_requirements.required_pressure)
      add_pressure(item.id);
    for (const auto &item : node.project_requirements.required_pressure_any)
      add_pressure(item.id);
    if (!pressure_ids.empty()) {
      double need = -std::numeric_limits<double>::infinity();
      for (const auto &id : pressure_ids)
        need = std::max(need, (civilization.get_pressure(id) +
            priority_score(agenda_state.get_problem_priority(
                id, catalog->runtime_policy().default_priority_id))) / 2);
      components.push_back({"recognized_need", need,
          "Recognized need " + one_decimal(need) +
          "/100 from known pressure and problem priority."});
    }
    double alignment = priority_score(agenda_state.get_domain_priority(
        node.domain_id, catalog->runtime_policy().default_priority_id));
    for (const auto &id : node.knowledge_fields)
      alignment = std::max(alignment, priority_score(
          agenda_state.get_field_priority(
              id, catalog->runtime_policy().default_priority_id)));
    for (const auto &id : node.declared_capabilities)
      if (authority->catalog().find_capability(id))
        alignment = std::max(alignment, priority_score(
            agenda_state.get_capability_priority(
                id, catalog->runtime_policy().default_priority_id)));
    components.push_back({"strategic_alignment", alignment,
        "Strategic agenda alignment " + one_decimal(alignment) + "/100."});
    components.push_back({"readiness", readiness.overall_readiness_score,
        "Current Project Readiness " +
        one_decimal(readiness.overall_readiness_score) + "/100."});

    int output_count{};
    int missing_count{};
    for (const auto &id : node.declared_capabilities)
      if (const auto *definition = authority->catalog().find_capability(id);
          definition && definition->scope == ResearchCapabilityScope::civilization) {
        ++output_count;
        if (!civilization.has_capability(id)) ++missing_count;
      }
    if (output_count > 0) {
      const auto gap = 100.0 * missing_count / output_count;
      components.push_back({"capability_gap_value", gap,
          "Known civilization capability-gap value " + one_decimal(gap) +
          "/100."});
    }

    const auto scaled_labs = authority->catalog().lab_scaling().scale_assigned_labs(
        requested_labs, node.project_requirements.recommended_labs);
    const auto rp_per_year = scaled_labs *
        authority->catalog().metadata().base_rp_per_effective_lab_per_year *
        readiness.rp_efficiency;
    const auto years = rp_per_year <= 0
        ? std::numeric_limits<double>::infinity()
        : node.project_requirements.base_research_points / rp_per_year;
    const auto time_score = std::isinf(years) ? 0.0
        : 100.0 / (1.0 + years /
            catalog->runtime_policy().time_to_effect_half_value_years);
    components.push_back({"time_to_effect", time_score,
        "Estimated research horizon " +
        (std::isinf(years) ? std::string("unbounded under current conditions")
                           : one_decimal(years) + "y") +
        "; time value " + one_decimal(time_score) + "/100."});
    const auto capacity = civilization.total_effective_research_labs();
    const auto lab_score = capacity <= 0 ? 0.0
        : 100.0 / (1.0 + requested_labs /
            std::max(0.000001, capacity *
                catalog->runtime_policy().lab_cost_half_value_fraction));
    components.push_back({"lab_opportunity_cost", lab_score,
        "Lab opportunity-cost desirability " + one_decimal(lab_score) +
        "/100."});

    bool mature_same_family{};
    for (const auto &existing : civilization.node_states()) {
      if (existing.counts_as_established_knowledge() &&
          existing.node_id != node.id &&
          authority->catalog().get_node(existing.node_id).solution_family ==
              node.solution_family) {
        mature_same_family = true;
        break;
      }
    }
    const auto alternative = mature_same_family
        ? catalog->runtime_policy().already_covered_solution_value
        : catalog->runtime_policy().novel_solution_value;
    components.push_back({"alternative_coverage", alternative,
        mature_same_family
          ? "A mature solution already covers this solution family."
          : "This would preserve/open a distinct solution family."});
    const auto diversity = (agenda_state.orientations().portfolio_diversity_policy +
        agenda_state.get_culture_axis("portfolio_diversity")) / 2;
    if (diversity > 50) {
      const auto diversity_score = mature_same_family ? 100 - diversity : diversity;
      components.push_back({"portfolio_diversity", diversity_score,
          "Portfolio-diversity value " + one_decimal(diversity_score) + "/100."});
    }
    if (node.is_hypothesis) {
      const auto risk = agenda_state.get_culture_axis("risk_tolerance");
      components.push_back({"uncertainty_risk", risk,
          "Hypothesis risk fit " + one_decimal(risk) + "/100."});
    }
    double immediate_need{};
    for (const auto &id : pressure_ids)
      immediate_need = std::max(immediate_need, civilization.get_pressure(id));
    if (immediate_need < 50) {
      const auto long_value =
          (agenda_state.get_culture_axis("curiosity") +
           agenda_state.get_culture_axis("long_term_orientation") +
           (100 - agenda_state.orientations().basic_vs_applied_orientation)) / 3;
      components.push_back({"long_horizon_value", long_value,
          "Long-horizon/basic-science value " + one_decimal(long_value) +
          "/100."});
    }
    if (!node.knowledge_fields.empty()) {
      double spillover{};
      for (const auto &id : node.knowledge_fields)
        spillover += 100 - civilization.expertise().get_field(id).current.theoretical;
      spillover /= static_cast<double>(node.knowledge_fields.size());
      components.push_back({"knowledge_spillover", spillover,
          "Potential active-competence learning value " +
          one_decimal(spillover) + "/100."});
    }
    double utility{};
    for (const auto &component : components) utility += component.score;
    if (!components.empty()) utility /= static_cast<double>(components.size());
    if (!eligibility.allowed)
      utility *= catalog->runtime_policy().blocked_project_score_multiplier;
    utility = std::clamp(utility, 0.0, 100.0);
    auto strongest = components;
    std::stable_sort(strongest.begin(), strongest.end(), [](const auto &left,
                                                            const auto &right) {
      return double_descending(left.score, right.score);
    });
    std::string factor_text;
    for (std::size_t index = 0; index < std::min<std::size_t>(3, strongest.size());
         ++index) {
      if (index) factor_text += ", ";
      factor_text += strongest[index].id;
    }
    const auto explanation = eligibility.allowed
        ? "Visible candidate score " + one_decimal(utility) +
              "/100; strongest factors: " + factor_text + "."
        : "Visible strategic candidate score " + one_decimal(utility) +
              "/100 after blocker reduction; requires real capacity/requirements before start.";
    return {node.id, utility, eligibility.allowed, requested_labs, years,
            std::move(components), eligibility.blockers, explanation};
  }
};

AdaptiveResearchAgendaRuntime::AdaptiveResearchAgendaRuntime(
    const AdaptiveResearchAuthority &authority,
    const AdaptiveResearchAgendaCatalog &catalog)
    : storage_(std::make_unique<Storage>(authority, catalog)) {}
AdaptiveResearchAgendaRuntime::~AdaptiveResearchAgendaRuntime() = default;
AdaptiveResearchAgendaRuntime::AdaptiveResearchAgendaRuntime(
    AdaptiveResearchAgendaRuntime &&) noexcept = default;
AdaptiveResearchAgendaRuntime &AdaptiveResearchAgendaRuntime::operator=(
    AdaptiveResearchAgendaRuntime &&) noexcept = default;
const AdaptiveResearchAgendaCatalog &AdaptiveResearchAgendaRuntime::catalog() const noexcept { return *storage_->catalog; }
const AdaptiveResearchAgendaState &AdaptiveResearchAgendaRuntime::state(const AdaptiveResearchCivilizationState &civilization) const { return storage_->agenda(civilization); }

void AdaptiveResearchAgendaRuntime::set_domain_priority(const AdaptiveResearchCivilizationState &civilization, std::string_view domain_id, std::string_view priority_id) const {
  if (std::ranges::none_of(storage_->authority->catalog().nodes(), [&](const auto &node) { return node.domain_id == domain_id; }))
    throw std::invalid_argument("Unknown research domain '" + std::string(domain_id) + "'. (Parameter 'domainId')");
  storage_->set_priority(civilization, &AdaptiveResearchAgendaState::Storage::domain_priorities, std::string(domain_id), std::string(priority_id));
}
void AdaptiveResearchAgendaRuntime::set_field_priority(const AdaptiveResearchCivilizationState &civilization, std::string_view field_id, std::string_view priority_id) const {
  if (std::ranges::none_of(storage_->authority->expertise_catalog().fields(), [&](const auto &field) { return field.id == field_id; }))
    throw std::invalid_argument("Unknown research field '" + std::string(field_id) + "'. (Parameter 'fieldId')");
  storage_->set_priority(civilization, &AdaptiveResearchAgendaState::Storage::field_priorities, std::string(field_id), std::string(priority_id));
}
void AdaptiveResearchAgendaRuntime::set_problem_priority(const AdaptiveResearchCivilizationState &civilization, std::string_view pressure_id, std::string_view priority_id) const {
  if (!storage_->authority->catalog().has_pressure(pressure_id))
    throw std::invalid_argument("Unknown Research Pressure '" + std::string(pressure_id) + "'. (Parameter 'pressureId')");
  storage_->set_priority(civilization, &AdaptiveResearchAgendaState::Storage::problem_priorities, std::string(pressure_id), std::string(priority_id));
}
void AdaptiveResearchAgendaRuntime::set_capability_priority(const AdaptiveResearchCivilizationState &civilization, std::string_view capability_id, std::string_view priority_id) const {
  if (!storage_->authority->catalog().find_capability(capability_id))
    throw std::invalid_argument("Unknown cross-lineage capability '" + std::string(capability_id) + "'. (Parameter 'capabilityId')");
  storage_->set_priority(civilization, &AdaptiveResearchAgendaState::Storage::capability_priorities, std::string(capability_id), std::string(priority_id));
}
void AdaptiveResearchAgendaRuntime::set_scientific_culture_axis(const AdaptiveResearchCivilizationState &civilization, std::string_view axis_id, double value) const {
  if (std::ranges::none_of(storage_->catalog->culture_axes(), [&](const auto &axis) { return axis.id == axis_id; }))
    throw std::invalid_argument("Unknown scientific-culture axis '" + std::string(axis_id) + "'. (Parameter 'axisId')");
  AgendaWriter::set_culture_axis(storage_->agenda(civilization), std::string(axis_id), value);
  storage_->invalidate(civilization);
}
void AdaptiveResearchAgendaRuntime::set_orientations(const AdaptiveResearchCivilizationState &civilization, ResearchAgendaOrientationState value) const {
  AgendaWriter::set_orientations(storage_->agenda(civilization), value);
  storage_->invalidate(civilization);
}

ResearchAgendaReviewRecommendation AdaptiveResearchAgendaRuntime::evaluate_perceived_adequacy(const AdaptiveResearchCivilizationState &civilization, const ResearchPerceivedAdequacyAssessment &assessment) const {
  validate100(assessment.adequacy, "Adequacy");
  validate100(assessment.credible_challenge, "CredibleChallenge");
  if (assessment.confidence < 0 || assessment.confidence > 1 || std::isnan(assessment.confidence))
    throw std::out_of_range("Specified argument was out of the range of valid values. (Parameter 'Confidence')");
  if (std::ranges::none_of(storage_->authority->catalog().nodes(), [&](const auto &node) { return node.domain_id == assessment.domain_id; }))
    throw std::invalid_argument("Unknown research domain '" + assessment.domain_id + "'. (Parameter 'assessment')");
  const auto &agenda_state = storage_->agenda(civilization);
  const auto &policy = storage_->catalog->runtime_policy();
  const auto pressure = storage_->visible_domain_pressure(civilization, assessment.domain_id);
  const auto threat = agenda_state.get_culture_axis("threat_sensitivity");
  const auto culture = (agenda_state.get_culture_axis("complacency_tendency") +
      agenda_state.get_culture_axis("institutional_conservatism") + 100 - threat) / 3;
  const auto complacency = assessment.adequacy * assessment.confidence * culture / 100;
  const auto challenge = assessment.credible_challenge * assessment.confidence * threat / 100;
  std::string recommended(agenda_state.get_domain_priority(assessment.domain_id, policy.default_priority_id));
  std::string explanation;
  if (assessment.confidence < policy.minimum_adequacy_confidence)
    explanation = "No agenda change recommended because the adequacy/threat assessment confidence is too low.";
  else if (challenge >= policy.critical_challenge_threshold) { recommended="critical"; explanation="Critical attention recommended because a legitimately observed challenge is severe and this civilization is highly threat-sensitive."; }
  else if (challenge >= policy.strategic_challenge_threshold) { recommended="strategic"; explanation="Strategic attention recommended because credible observed competition/threat is strong."; }
  else if (challenge >= policy.important_challenge_threshold) { recommended="important"; explanation="Important attention recommended because credible observed competition/threat is rising."; }
  else if (assessment.adequacy >= policy.high_adequacy_minimum && pressure <= policy.low_relevant_pressure_maximum && complacency >= policy.complacency_deprioritize_threshold) { recommended="deprioritized"; explanation="Deprioritization is plausible because current capability is perceived as adequate, recognized need is low, and complacency/conservatism outweigh threat sensitivity."; }
  else explanation="Current agenda priority remains reasonable under the legitimate adequacy, pressure, and challenge information available.";
  return {assessment.domain_id, std::move(recommended), pressure, complacency, challenge, std::move(explanation)};
}

void AdaptiveResearchAgendaRuntime::apply_recommendation(const AdaptiveResearchCivilizationState &civilization, const ResearchAgendaReviewRecommendation &recommendation, double year, std::string_view provenance) const {
  const std::string domain = recommendation.domain_id;
  const std::string priority = recommendation.recommended_priority_id;
  const std::string owned_provenance(provenance);
  set_domain_priority(civilization, domain, priority);
  AgendaWriter::mark_reviewed(storage_->agenda(civilization), year,
                              owned_provenance);
  storage_->invalidate(civilization);
}

std::vector<ResearchVisibleProjectCandidate> AdaptiveResearchAgendaRuntime::build_visible_shortlist(const AdaptiveResearchCivilizationState &civilization) const {
  auto &agenda_state = storage_->agenda(civilization);
  auto &cache = storage_->caches.get_or_create(civilization);
  if (cache.valid && cache.core_revision == civilization.materialized_view_revision() &&
      cache.expertise_revision == civilization.expertise().revision() &&
      cache.agenda_revision == agenda_state.revision())
    return cache.candidates;
  std::vector<ResearchVisibleProjectCandidate> candidates;
  for (const auto &node : civilization.node_states())
    if (node.maturity == ResearchMaturity::investigable &&
        !has_project(civilization, node.node_id))
      candidates.push_back(storage_->score(civilization, agenda_state, node.node_id));
  std::stable_sort(candidates.begin(), candidates.end(), [](const auto &left, const auto &right) {
    if (left.utility_score != right.utility_score &&
        !(std::isnan(left.utility_score) && std::isnan(right.utility_score)))
      return double_descending(left.utility_score, right.utility_score);
    return ordinal_less(left.node_id, right.node_id);
  });
  if (candidates.size() > static_cast<std::size_t>(storage_->catalog->shortlist_bound()))
    candidates.resize(static_cast<std::size_t>(storage_->catalog->shortlist_bound()));
  cache.valid = true;
  cache.core_revision = civilization.materialized_view_revision();
  cache.expertise_revision = civilization.expertise().revision();
  cache.agenda_revision = agenda_state.revision();
  cache.rebuild_count++;
  cache.candidates = candidates;
  return candidates;
}

std::uint64_t detail::AdaptiveResearchAgendaRuntimeTestAccess::shortlist_rebuild_count(const AdaptiveResearchAgendaRuntime &runtime, const AdaptiveResearchCivilizationState &civilization) noexcept {
  if (const auto *cache = runtime.storage_->caches.try_get(civilization)) return cache->rebuild_count;
  return 0;
}
void detail::AdaptiveResearchAgendaRuntimeTestAccess::maintain(AdaptiveResearchAgendaRuntime &runtime, std::size_t limit) { runtime.storage_->states.maintain(limit); runtime.storage_->caches.maintain(limit); }
std::size_t detail::AdaptiveResearchAgendaRuntimeTestAccess::state_count(const AdaptiveResearchAgendaRuntime &runtime) noexcept { return runtime.storage_->states.entry_count_for_testing(); }
std::size_t detail::AdaptiveResearchAgendaRuntimeTestAccess::cache_count(const AdaptiveResearchAgendaRuntime &runtime) noexcept { return runtime.storage_->caches.entry_count_for_testing(); }

AdaptiveResearchAgendaState &detail::AdaptiveResearchAgendaSupportAccess::state(
    const AdaptiveResearchAgendaRuntime &runtime,
    const AdaptiveResearchCivilizationState &civilization) {
  return runtime.storage_->agenda(civilization);
}

} // namespace stellar::core
