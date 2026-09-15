#include <stellar/core/adaptive_research_expertise.hpp>
#include <stellar/core/detail/adaptive_research_expertise_state_writer.hpp>

#include <charconv>
#include <cmath>
#include <limits>
#include <stdexcept>
#include <string_view>
#include <unordered_map>
#include <utility>

namespace stellar::core {
namespace {

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

template <class Value> class SlotMap {
  struct Entry {
    std::string key;
    Value value;
    bool active{true};
  };
  std::vector<Entry> entries_;
  std::vector<std::size_t> free_;
  std::unordered_map<std::string, std::size_t, TransparentHash,
                     TransparentEqual>
      index_;

public:
  const Value *find(std::string_view key) const noexcept {
    const auto found = index_.find(key);
    return found == index_.end() ? nullptr : &entries_[found->second].value;
  }

  template <class V> void set(std::string key, V &&value) {
    const auto found = index_.find(key);
    if (found != index_.end()) {
      entries_[found->second].value = std::forward<V>(value);
      return;
    }
    std::size_t position{};
    if (free_.empty()) {
      position = entries_.size();
      entries_.push_back({std::move(key), std::forward<V>(value), true});
    } else {
      position = free_.back();
      free_.pop_back();
      entries_[position] = {std::move(key), std::forward<V>(value), true};
    }
    index_.emplace(entries_[position].key, position);
  }

  bool erase(std::string_view key) {
    const auto found = index_.find(key);
    if (found == index_.end()) {
      return false;
    }
    entries_[found->second].active = false;
    free_.push_back(found->second);
    index_.erase(found);
    return true;
  }

  template <class Function> void each(Function function) const {
    for (const auto &entry : entries_) {
      if (entry.active) {
        function(entry.value);
      }
    }
  }
};

std::int64_t next_revision(std::int64_t current) {
  if (current == std::numeric_limits<std::int64_t>::max()) {
    throw std::overflow_error(
        "Adaptive Research expertise revision exhausted.");
  }
  return current + 1;
}

void validate_unit(double value, std::string_view name, double maximum) {
  if (value < 0.0 || value > maximum || !std::isfinite(value)) {
    const auto number_text = [](double number) {
      if (std::isnan(number)) {
        return std::string("NaN");
      }
      if (std::isinf(number)) {
        return number > 0.0 ? std::string("Infinity")
                            : std::string("-Infinity");
      }
      char buffer[64];
      const auto result =
          std::to_chars(buffer, buffer + sizeof(buffer), number);
      auto text = std::string(buffer, result.ptr);
      if (const auto exponent = text.find('e'); exponent != std::string::npos) {
        text[exponent] = 'E';
      }
      return text;
    };
    throw std::out_of_range("Value must be in 0.." + number_text(maximum) +
                            ". (Parameter '" + std::string(name) +
                            "')\r\nActual value was " + number_text(value) +
                            ".");
  }
}

void validate_vector(const ResearchCompetenceVector &value,
                     std::string_view field_id) {
  validate_unit(value.theoretical, std::string(field_id) + ".theoretical",
                100.0);
  validate_unit(value.experimental, std::string(field_id) + ".experimental",
                100.0);
  validate_unit(value.engineering, std::string(field_id) + ".engineering",
                100.0);
}

} // namespace

std::int64_t detail::checked_next_adaptive_research_expertise_revision(
    std::int64_t current) {
  return next_revision(current);
}

struct AdaptiveResearchExpertiseState::Storage {
  std::int64_t revision{};
  SlotMap<ResearchFieldCompetenceRuntimeState> fields;
  SlotMap<ResearchInstitutionRuntimeState> institutions;
  SlotMap<ResearchTacitAssetRuntimeState> tacit_assets;
  std::vector<ResearchFieldCompetenceRuntimeState> field_cache;
  std::vector<ResearchInstitutionRuntimeState> institution_cache;
  std::vector<ResearchTacitAssetRuntimeState> tacit_cache;

  void rebuild_fields() {
    field_cache.clear();
    fields.each([&](const auto &value) { field_cache.push_back(value); });
  }
  void rebuild_institutions() {
    institution_cache.clear();
    institutions.each(
        [&](const auto &value) { institution_cache.push_back(value); });
  }
  void rebuild_tacit_assets() {
    tacit_cache.clear();
    tacit_assets.each([&](const auto &value) { tacit_cache.push_back(value); });
  }
};

AdaptiveResearchExpertiseState::AdaptiveResearchExpertiseState()
    : storage_(std::make_unique<Storage>()) {}
AdaptiveResearchExpertiseState::~AdaptiveResearchExpertiseState() = default;
AdaptiveResearchExpertiseState::AdaptiveResearchExpertiseState(
    const AdaptiveResearchExpertiseState &other)
    : storage_(std::make_unique<Storage>(*other.storage_)) {}
AdaptiveResearchExpertiseState &AdaptiveResearchExpertiseState::operator=(
    const AdaptiveResearchExpertiseState &other) {
  if (this != &other) {
    storage_ = std::make_unique<Storage>(*other.storage_);
  }
  return *this;
}
AdaptiveResearchExpertiseState::AdaptiveResearchExpertiseState(
    AdaptiveResearchExpertiseState &&) noexcept = default;
AdaptiveResearchExpertiseState &AdaptiveResearchExpertiseState::operator=(
    AdaptiveResearchExpertiseState &&) noexcept = default;

std::int64_t AdaptiveResearchExpertiseState::revision() const noexcept {
  return storage_->revision;
}
std::span<const ResearchFieldCompetenceRuntimeState>
AdaptiveResearchExpertiseState::field_competence() const noexcept {
  return storage_->field_cache;
}
std::span<const ResearchInstitutionRuntimeState>
AdaptiveResearchExpertiseState::institutions() const noexcept {
  return storage_->institution_cache;
}
std::span<const ResearchTacitAssetRuntimeState>
AdaptiveResearchExpertiseState::tacit_assets() const noexcept {
  return storage_->tacit_cache;
}
ResearchFieldCompetenceRuntimeState
AdaptiveResearchExpertiseState::get_field(std::string_view field_id) const {
  if (const auto *value = storage_->fields.find(field_id)) {
    return *value;
  }
  const auto negative_infinity = -std::numeric_limits<double>::infinity();
  return {
      .field_id = std::string(field_id),
      .current = {},
      .historical_peak = {},
      .last_theoretical_activity_year = negative_infinity,
      .last_experimental_activity_year = negative_infinity,
      .last_engineering_activity_year = negative_infinity,
      .revision = storage_->revision,
  };
}
double AdaptiveResearchExpertiseState::total_active_effective_lab_units(
    const AdaptiveResearchExpertiseCatalog &catalog) const {
  double total{};
  storage_->institutions.each([&](const auto &institution) {
    const auto &definition =
        catalog.get_institution(institution.institution_archetype_id);
    total += definition.effective_lab_units * institution.active_count;
  });
  return total;
}

void detail::AdaptiveResearchExpertiseStateWriter::set_field(
    AdaptiveResearchExpertiseState &state,
    ResearchFieldCompetenceRuntimeState value) {
  validate_vector(value.current, value.field_id);
  validate_vector(value.historical_peak, value.field_id);
  const auto revision = next_revision(state.storage_->revision);
  value.revision = revision;
  auto field_id = value.field_id;
  state.storage_->fields.set(std::move(field_id), std::move(value));
  state.storage_->revision = revision;
  state.storage_->rebuild_fields();
}
void detail::AdaptiveResearchExpertiseStateWriter::remove_field_if_zero(
    AdaptiveResearchExpertiseState &state, std::string_view field_id) {
  const auto *value = state.storage_->fields.find(field_id);
  if (value == nullptr || value->current.theoretical > 0.0 ||
      value->current.experimental > 0.0 || value->current.engineering > 0.0 ||
      value->historical_peak.theoretical > 0.0 ||
      value->historical_peak.experimental > 0.0 ||
      value->historical_peak.engineering > 0.0) {
    return;
  }
  const auto revision = next_revision(state.storage_->revision);
  state.storage_->fields.erase(field_id);
  state.storage_->revision = revision;
  state.storage_->rebuild_fields();
}
void detail::AdaptiveResearchExpertiseStateWriter::set_institution(
    AdaptiveResearchExpertiseState &state,
    ResearchInstitutionRuntimeState value) {
  if (value.total_count < 0 || value.active_count < 0 ||
      value.active_count > value.total_count) {
    throw std::out_of_range("Invalid institution counts for '" +
                            value.institution_instance_id +
                            "'. (Parameter 'state')");
  }
  if (value.total_count == 0) {
    if (!state.storage_->institutions.find(value.institution_instance_id)) {
      return;
    }
    const auto revision = next_revision(state.storage_->revision);
    state.storage_->institutions.erase(value.institution_instance_id);
    state.storage_->revision = revision;
    state.storage_->rebuild_institutions();
    return;
  }
  const auto revision = next_revision(state.storage_->revision);
  value.revision = revision;
  auto institution_id = value.institution_instance_id;
  state.storage_->institutions.set(std::move(institution_id), std::move(value));
  state.storage_->revision = revision;
  state.storage_->rebuild_institutions();
}
bool detail::AdaptiveResearchExpertiseStateWriter::remove_institution(
    AdaptiveResearchExpertiseState &state,
    std::string_view institution_instance_id) {
  if (!state.storage_->institutions.find(institution_instance_id)) {
    return false;
  }
  const auto revision = next_revision(state.storage_->revision);
  state.storage_->institutions.erase(institution_instance_id);
  state.storage_->revision = revision;
  state.storage_->rebuild_institutions();
  return true;
}
void detail::AdaptiveResearchExpertiseStateWriter::set_tacit_asset(
    AdaptiveResearchExpertiseState &state,
    ResearchTacitAssetRuntimeState value) {
  validate_unit(value.depth, "Depth", 100.0);
  validate_unit(value.availability, "Availability", 1.0);
  validate_unit(value.translation_context_quality, "TranslationContextQuality",
                1.0);
  validate_unit(value.training_continuity, "TrainingContinuity", 1.0);
  const auto revision = next_revision(state.storage_->revision);
  value.revision = revision;
  auto asset_id = value.asset_id;
  state.storage_->tacit_assets.set(std::move(asset_id), std::move(value));
  state.storage_->revision = revision;
  state.storage_->rebuild_tacit_assets();
}
bool detail::AdaptiveResearchExpertiseStateWriter::remove_tacit_asset(
    AdaptiveResearchExpertiseState &state, std::string_view asset_id) {
  if (!state.storage_->tacit_assets.find(asset_id)) {
    return false;
  }
  const auto revision = next_revision(state.storage_->revision);
  state.storage_->tacit_assets.erase(asset_id);
  state.storage_->revision = revision;
  state.storage_->rebuild_tacit_assets();
  return true;
}

} // namespace stellar::core
