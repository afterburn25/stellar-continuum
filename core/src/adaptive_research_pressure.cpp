#include <stellar/engine/asset_registry.hpp>
#include <stellar/core/adaptive_research_pressure.hpp>

#include <stellar/core/detail/adaptive_research_pressure_state_writer.hpp>
#include <stellar/core/detail/adaptive_research_pressure_support_access.hpp>
#include <stellar/core/detail/adaptive_research_weak_state_table.hpp>

#include <nlohmann/json.hpp>

#include <algorithm>
#include <charconv>
#include <cmath>
#include <cstdint>
#include <fstream>
#include <limits>
#include <stdexcept>
#include <unordered_map>
#include <unordered_set>
#include <utility>

namespace stellar::core {
namespace {
using Json = nlohmann::ordered_json;

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

template <class Value>
using Lookup =
    std::unordered_map<std::string, Value, TransparentHash, TransparentEqual>;

std::vector<std::uint16_t> utf16_units(std::string_view value) {
  std::vector<std::uint16_t> result;
  while (!value.empty()) {
    const auto first = static_cast<unsigned char>(value.front());
    std::uint32_t code_point{};
    std::size_t width{};
    if (first <= 0x7f) {
      code_point = first;
      width = 1;
    } else if (first >= 0xc2 && first <= 0xdf) {
      code_point = first & 0x1f;
      width = 2;
    } else if (first >= 0xe0 && first <= 0xef) {
      code_point = first & 0x0f;
      width = 3;
    } else if (first >= 0xf0 && first <= 0xf4) {
      code_point = first & 0x07;
      width = 4;
    } else {
      throw std::invalid_argument("Research identifier is not valid UTF-8.");
    }
    if (value.size() < width)
      throw std::invalid_argument("Research identifier is not valid UTF-8.");
    for (std::size_t index = 1; index < width; ++index) {
      const auto byte = static_cast<unsigned char>(value[index]);
      if ((byte & 0xc0) != 0x80)
        throw std::invalid_argument("Research identifier is not valid UTF-8.");
      code_point = (code_point << 6) | (byte & 0x3f);
    }
    if ((width == 3 && (code_point < 0x800 ||
                        (code_point >= 0xd800 && code_point <= 0xdfff))) ||
        (width == 4 && (code_point < 0x10000 || code_point > 0x10ffff)))
      throw std::invalid_argument("Research identifier is not valid UTF-8.");
    if (code_point <= 0xffff) {
      result.push_back(static_cast<std::uint16_t>(code_point));
    } else {
      code_point -= 0x10000;
      result.push_back(static_cast<std::uint16_t>(0xd800 + (code_point >> 10)));
      result.push_back(
          static_cast<std::uint16_t>(0xdc00 + (code_point & 0x3ff)));
    }
    value.remove_prefix(width);
  }
  return result;
}

bool ordinal_less(std::string_view left, std::string_view right) {
  return utf16_units(left) < utf16_units(right);
}

Json read_json(const std::filesystem::path &path,
               bool reject_duplicate_rule_ids = false) {
  auto input=stellar::engine::resource_stream(path);
  if (!input)
    throw std::runtime_error("Could not open " + path.string());
  if (!reject_duplicate_rule_ids) {
    Json value;
    input >> value;
    return value;
  }
  bool rules_pending = false;
  std::optional<int> rules_depth;
  std::unordered_set<std::string> rule_ids;
  const Json::parser_callback_t callback = [&](int depth,
                                               Json::parse_event_t event,
                                               Json &parsed) {
    if (event == Json::parse_event_t::key) {
      const auto key = parsed.get<std::string>();
      if (rules_depth && depth == *rules_depth + 1 &&
          !rule_ids.insert(key).second)
        throw std::invalid_argument(
            "An item with the same key has already been added. Key: " + key);
      rules_pending = !rules_depth && depth == 1 && key == "rules";
    } else if (event == Json::parse_event_t::object_start && rules_pending) {
      rules_depth = depth;
      rules_pending = false;
    } else if (event == Json::parse_event_t::object_end && rules_depth &&
               depth == *rules_depth) {
      rules_depth.reset();
    }
    return true;
  };
  return Json::parse(input, callback);
}

std::vector<std::string> strings(const Json &value, std::string_view name) {
  const auto found = value.find(std::string(name));
  if (found == value.end())
    return {};
  if (!found->is_array())
    throw std::runtime_error("The requested operation requires an element of "
                             "type 'Array', but the target element has type '" +
                             std::string(found->type_name()) + "'.");
  std::vector<std::string> result;
  result.reserve(found->size());
  for (const auto &entry : *found) {
    if (entry.is_null())
      throw std::runtime_error(std::string(name) + " contains null.");
    if (!entry.is_string())
      throw std::runtime_error(
          "The requested operation requires an element of type 'String', but "
          "the target element has type '" +
          std::string(entry.type_name()) + "'.");
    result.push_back(entry.get<std::string>());
  }
  return result;
}

void validate_catalog_id(const Json &root, std::string_view expected,
                         std::string_view filename) {
  const auto actual = root.at("catalog_id").get<std::string>();
  if (actual != expected)
    throw std::runtime_error(std::string(filename) + " catalog_id '" + actual +
                             "' does not match '" + std::string(expected) +
                             "'.");
}

const Json &required_property(const Json &value, std::string_view name) {
  const auto found = value.find(std::string(name));
  if (found == value.end())
    throw std::out_of_range("The given key was not present in the dictionary.");
  return *found;
}

bool consume_dotnet_whitespace(std::string_view &value) noexcept {
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
  if (value.size() < length)
    return false;
  for (std::size_t index = 1; index < length; ++index) {
    const auto continuation = static_cast<unsigned char>(value[index]);
    if ((continuation & 0xc0) != 0x80)
      return false;
    code_point = (code_point << 6) | (continuation & 0x3f);
  }
  value.remove_prefix(length);
  return (code_point >= 0x09 && code_point <= 0x0d) || code_point == 0x20 ||
         code_point == 0x85 || code_point == 0xa0 || code_point == 0x1680 ||
         (code_point >= 0x2000 && code_point <= 0x200a) ||
         code_point == 0x2028 || code_point == 0x2029 || code_point == 0x202f ||
         code_point == 0x205f || code_point == 0x3000;
}
bool blank(std::string_view value) noexcept {
  if (value.empty())
    return true;
  while (!value.empty())
    if (!consume_dotnet_whitespace(value))
      return false;
  return true;
}

double dotnet_max(double left, double right) noexcept {
  return std::isnan(left) || std::isnan(right)
             ? std::numeric_limits<double>::quiet_NaN()
             : std::max(left, right);
}

double clamp(double value) noexcept {
  if (std::isnan(value))
    return value;
  return std::clamp(value, 0.0, 100.0);
}

std::int64_t next_pressure_revision(std::int64_t value) {
  if (value == std::numeric_limits<std::int64_t>::max())
    throw std::overflow_error(
        "Adaptive Research pressure-state revision space is exhausted.");
  return value + 1;
}

std::string dotnet_number(double value) {
  if (std::isnan(value))
    return "NaN";
  if (value == std::numeric_limits<double>::infinity())
    return "Infinity";
  if (value == -std::numeric_limits<double>::infinity())
    return "-Infinity";
  char buffer[64];
  const auto result =
      std::to_chars(std::begin(buffer), std::end(buffer), value);
  if (result.ec != std::errc{})
    throw std::runtime_error("Failed to format a floating-point value.");
  return std::string(buffer, result.ptr);
}

template <class Value> class SlotMap {
  struct Entry {
    std::string key;
    Value value;
    bool active{true};
  };
  std::vector<Entry> entries_;
  std::vector<std::size_t> free_;
  Lookup<std::size_t> index_;

public:
  Value *find(std::string_view key) noexcept {
    const auto found = index_.find(key);
    return found == index_.end() ? nullptr : &entries_[found->second].value;
  }
  const Value *find(std::string_view key) const noexcept {
    const auto found = index_.find(key);
    return found == index_.end() ? nullptr : &entries_[found->second].value;
  }
  bool set(std::string key, Value value) {
    if (auto *existing = find(key)) {
      *existing = std::move(value);
      return false;
    }
    std::size_t position{};
    if (free_.empty()) {
      position = entries_.size();
      entries_.push_back({std::move(key), std::move(value), true});
    } else {
      position = free_.back();
      free_.pop_back();
      entries_[position] = {std::move(key), std::move(value), true};
    }
    index_.emplace(entries_[position].key, position);
    return true;
  }
  bool erase(std::string_view key) {
    const auto found = index_.find(key);
    if (found == index_.end())
      return false;
    entries_[found->second].active = false;
    free_.push_back(found->second);
    index_.erase(found);
    return true;
  }
  template <class Function> void each(Function function) const {
    for (const auto &entry : entries_)
      if (entry.active)
        function(entry.key, entry.value);
  }
};

class SlotSet {
  SlotMap<bool> values_;
  std::vector<std::string> cache_;

public:
  bool insert(std::string value) {
    if (!values_.set(std::move(value), true))
      return false;
    rebuild();
    return true;
  }
  bool erase(std::string_view value) {
    if (!values_.erase(value))
      return false;
    rebuild();
    return true;
  }
  std::span<const std::string> values() const noexcept { return cache_; }

private:
  void rebuild() {
    cache_.clear();
    values_.each([&](const auto &key, bool) { cache_.push_back(key); });
  }
};
} // namespace

struct AdaptiveResearchPressureCatalog::Storage {
  std::vector<ResearchPressureRuleDefinition> rules;
  Lookup<std::size_t> rule_index;
  std::vector<std::pair<std::string, std::vector<std::string>>> metric_index;
  std::vector<std::pair<std::string, std::vector<std::string>>> event_index;
  Lookup<std::size_t> metric_lookup;
  Lookup<std::size_t> event_lookup;
  ResearchPressureRuntimePolicy policy;
};

AdaptiveResearchPressureCatalog::AdaptiveResearchPressureCatalog(
    std::unique_ptr<Storage> storage) noexcept
    : storage_(std::move(storage)) {}
AdaptiveResearchPressureCatalog::~AdaptiveResearchPressureCatalog() = default;
AdaptiveResearchPressureCatalog::AdaptiveResearchPressureCatalog(
    AdaptiveResearchPressureCatalog &&) noexcept = default;
AdaptiveResearchPressureCatalog &AdaptiveResearchPressureCatalog::operator=(
    AdaptiveResearchPressureCatalog &&) noexcept = default;
std::span<const ResearchPressureRuleDefinition>
AdaptiveResearchPressureCatalog::rules() const noexcept {
  return storage_->rules;
}
const ResearchPressureRuntimePolicy &
AdaptiveResearchPressureCatalog::runtime_policy() const noexcept {
  return storage_->policy;
}
bool AdaptiveResearchPressureCatalog::is_known_metric_signal(
    std::string_view signal_id) const noexcept {
  return storage_->metric_lookup.contains(signal_id);
}
bool AdaptiveResearchPressureCatalog::is_known_event_signal(
    std::string_view signal_id) const noexcept {
  return storage_->event_lookup.contains(signal_id);
}
const ResearchPressureRuleDefinition &
AdaptiveResearchPressureCatalog::get_rule(std::string_view pressure_id) const {
  const auto found = storage_->rule_index.find(pressure_id);
  if (found == storage_->rule_index.end())
    throw std::out_of_range("Unknown Research Pressure '" +
                            std::string(pressure_id) + "'.");
  return storage_->rules[found->second];
}
std::span<const std::string>
AdaptiveResearchPressureCatalog::pressures_for_metric_signal(
    std::string_view signal_id) const noexcept {
  const auto found = storage_->metric_lookup.find(signal_id);
  return found == storage_->metric_lookup.end()
             ? std::span<const std::string>{}
             : std::span<const std::string>(
                   storage_->metric_index[found->second].second);
}
std::span<const std::string>
AdaptiveResearchPressureCatalog::pressures_for_event_signal(
    std::string_view signal_id) const noexcept {
  const auto found = storage_->event_lookup.find(signal_id);
  return found == storage_->event_lookup.end()
             ? std::span<const std::string>{}
             : std::span<const std::string>(
                   storage_->event_index[found->second].second);
}

AdaptiveResearchPressureCatalog load_adaptive_research_pressure_catalog(
    const std::filesystem::path &root_path,
    const AdaptiveResearchCatalog &research_catalog) {
  auto storage = std::make_unique<AdaptiveResearchPressureCatalog::Storage>();
  const auto dynamics = read_json(root_path / "pressure_dynamics.json", true);
  validate_catalog_id(dynamics, research_catalog.metadata().catalog_id,
                      "pressure_dynamics.json");
  std::vector<std::pair<std::string, std::vector<std::string>>> metric;
  std::vector<std::pair<std::string, std::vector<std::string>>> event;
  Lookup<std::size_t> metric_lookup;
  Lookup<std::size_t> event_lookup;
  const auto add_index = [](auto &entries, auto &lookup,
                            const std::string &signal,
                            const std::string &pressure) {
    auto found = lookup.find(signal);
    if (found == lookup.end()) {
      const auto position = entries.size();
      entries.emplace_back(signal, std::vector<std::string>{});
      lookup.emplace(signal, position);
      found = lookup.find(signal);
    }
    auto &values = entries[found->second].second;
    if (std::ranges::find(values, pressure) == values.end())
      values.push_back(pressure);
  };
  const auto &rules = dynamics.at("rules");
  if (!rules.is_object())
    throw std::runtime_error(
        "The requested operation requires an element of type 'Object', but the "
        "target element has type '" +
        std::string(rules.type_name()) + "'.");
  for (const auto &[pressure_id, value] : rules.items()) {
    if (!research_catalog.has_pressure(pressure_id))
      throw std::runtime_error("Pressure dynamics defines unknown pressure '" +
                               pressure_id + "'.");
    auto metric_signals = strings(value, "metric_signals");
    auto event_signals = strings(value, "event_signals");
    const auto decay = value.at("decay_per_year").get<double>();
    const auto memory_floor = value.at("memory_floor").get<double>();
    if (decay < 0 || memory_floor < 0 || memory_floor > 100 ||
        std::isnan(decay) || std::isinf(decay))
      throw std::runtime_error("Pressure '" + pressure_id +
                               "' has invalid decay/memory-floor values.");
    const auto index = storage->rules.size();
    if (!storage->rule_index.emplace(pressure_id, index).second)
      throw std::invalid_argument(
          "An item with the same key has already been added. Key: " +
          pressure_id);
    storage->rules.push_back(
        {pressure_id, metric_signals, event_signals, decay, memory_floor});
    for (const auto &signal : metric_signals)
      add_index(metric, metric_lookup, signal, pressure_id);
    for (const auto &signal : event_signals)
      add_index(event, event_lookup, signal, pressure_id);
  }
  if (storage->rules.size() != research_catalog.pressure_ids().size() ||
      std::ranges::any_of(research_catalog.pressure_ids(), [&](const auto &id) {
        return !storage->rule_index.contains(id);
      }))
    throw std::runtime_error("Pressure dynamics rules do not cover the full "
                             "public Research Pressure catalog.");
  for (auto &[signal, values] : metric) {
    std::ranges::sort(values, ordinal_less);
    storage->metric_lookup.emplace(signal, storage->metric_index.size());
    storage->metric_index.emplace_back(signal, std::move(values));
  }
  for (auto &[signal, values] : event) {
    std::ranges::sort(values, ordinal_less);
    storage->event_lookup.emplace(signal, storage->event_index.size());
    storage->event_index.emplace_back(signal, std::move(values));
  }
  const auto policy_json =
      read_json(root_path / "research_pressure_runtime_policy.json");
  validate_catalog_id(policy_json, research_catalog.metadata().catalog_id,
                      "research_pressure_runtime_policy.json");
  const auto &metric_target = required_property(policy_json, "metric_target");
  const auto &response = policy_json.at("response");
  const auto &maintenance = policy_json.at("maintenance");
  storage->policy = {
      0.75,
      0.25,
      response.at("rise_toward_metric_target_per_year").get<double>(),
      response.at("event_pulse_base_points").get<double>(),
      maintenance.at("intended_review_interval_years").get<double>(),
      maintenance.at("coarse_dormant_review_interval_years").get<double>()};
  const auto &policy = storage->policy;
  if (policy.rise_toward_target_per_year <= 0 ||
      policy.event_pulse_base_points <= 0 ||
      policy.intended_review_interval_years <= 0 ||
      policy.dormant_review_interval_years <= 0 ||
      std::abs(policy.strongest_signal_weight + policy.mean_signal_weight - 1) >
          .000001)
    throw std::runtime_error("Invalid Research Pressure runtime policy.");
  (void)metric_target;
  return AdaptiveResearchPressureCatalog(std::move(storage));
}

struct AdaptiveResearchPressureState::Storage {
  std::int64_t revision{};
  SlotMap<double> metrics;
  SlotSet active;
  std::vector<ResearchPressureMetricSignalEntry> metric_cache;
  void rebuild_metrics() {
    metric_cache.clear();
    metrics.each([&](const auto &id, double value) {
      metric_cache.push_back({id, value});
    });
  }
};

class AdaptiveResearchPressureState::Writer {
public:
  static bool set_metric(AdaptiveResearchPressureState &state,
                         std::string signal_id, double value) {
    if (value < 0 || value > 1 || std::isnan(value) || std::isinf(value))
      throw std::out_of_range("Pressure metric signal must be in 0..1. "
                              "(Parameter 'normalizedValue')\r\n"
                              "Actual value was " +
                              dotnet_number(value) + ".");
    auto &storage = *state.storage_;
    if (value <= 0) {
      if (!storage.metrics.erase(signal_id))
        return false;
    } else {
      if (const auto *existing = storage.metrics.find(signal_id);
          existing && std::abs(*existing - value) < .0000001)
        return false;
      storage.metrics.set(std::move(signal_id), value);
    }
    storage.revision = next_pressure_revision(storage.revision);
    storage.rebuild_metrics();
    return true;
  }
  static bool activate(AdaptiveResearchPressureState &state,
                       std::string pressure_id) {
    auto &storage = *state.storage_;
    if (!storage.active.insert(std::move(pressure_id)))
      return false;
    storage.revision = next_pressure_revision(storage.revision);
    return true;
  }
  static bool deactivate(AdaptiveResearchPressureState &state,
                         std::string_view pressure_id) {
    auto &storage = *state.storage_;
    if (!storage.active.erase(pressure_id))
      return false;
    storage.revision = next_pressure_revision(storage.revision);
    return true;
  }
};

AdaptiveResearchPressureState::AdaptiveResearchPressureState()
    : storage_(std::make_unique<Storage>()) {}
AdaptiveResearchPressureState::~AdaptiveResearchPressureState() = default;
AdaptiveResearchPressureState::AdaptiveResearchPressureState(
    AdaptiveResearchPressureState &&) noexcept = default;
AdaptiveResearchPressureState &AdaptiveResearchPressureState::operator=(
    AdaptiveResearchPressureState &&) noexcept = default;
AdaptiveResearchPressureState::AdaptiveResearchPressureState(
    const AdaptiveResearchPressureState &other)
    : storage_(std::make_unique<Storage>(*other.storage_)) {}
AdaptiveResearchPressureState &AdaptiveResearchPressureState::operator=(
    const AdaptiveResearchPressureState &other) {
  if (this != &other)
    storage_ = std::make_unique<Storage>(*other.storage_);
  return *this;
}
std::int64_t AdaptiveResearchPressureState::revision() const noexcept {
  return storage_->revision;
}
std::span<const ResearchPressureMetricSignalEntry>
AdaptiveResearchPressureState::metric_signals() const noexcept {
  return storage_->metric_cache;
}
std::span<const std::string>
AdaptiveResearchPressureState::active_pressure_ids() const noexcept {
  return storage_->active.values();
}
double AdaptiveResearchPressureState::get_metric_signal(
    std::string_view signal_id) const noexcept {
  const auto *value = storage_->metrics.find(signal_id);
  return value ? *value : 0;
}

bool detail::AdaptiveResearchPressureStateWriter::set_metric_signal(
    AdaptiveResearchPressureState &state, std::string signal_id,
    double normalized_value) {
  return AdaptiveResearchPressureState::Writer::set_metric(
      state, std::move(signal_id), normalized_value);
}
bool detail::AdaptiveResearchPressureStateWriter::activate_pressure(
    AdaptiveResearchPressureState &state, std::string pressure_id) {
  return AdaptiveResearchPressureState::Writer::activate(
      state, std::move(pressure_id));
}
bool detail::AdaptiveResearchPressureStateWriter::deactivate_pressure(
    AdaptiveResearchPressureState &state, std::string_view pressure_id) {
  return AdaptiveResearchPressureState::Writer::deactivate(state, pressure_id);
}

struct AdaptiveResearchPressureRuntime::Storage {
  const AdaptiveResearchRuntime *kernel;
  const AdaptiveResearchPressureCatalog *catalog;
  mutable detail::AdaptiveResearchWeakStateTable<AdaptiveResearchPressureState>
      states;

  Storage(const AdaptiveResearchRuntime &runtime,
          const AdaptiveResearchPressureCatalog &pressure_catalog) noexcept
      : kernel(&runtime), catalog(&pressure_catalog) {}

  double metric_target(const AdaptiveResearchPressureState &support,
                       const ResearchPressureRuleDefinition &rule) const {
    std::vector<double> values;
    for (const auto &signal : rule.metric_signal_ids) {
      const auto value = support.get_metric_signal(signal);
      if (value > 0)
        values.push_back(value);
    }
    if (values.empty())
      return 0;
    const auto strongest = *std::ranges::max_element(values);
    double sum = 0;
    for (const auto value : values)
      sum += value;
    const auto mean = sum / static_cast<double>(values.size());
    const auto &policy = catalog->runtime_policy();
    return clamp(100 * (policy.strongest_signal_weight * strongest +
                        policy.mean_signal_weight * mean));
  }
};

AdaptiveResearchPressureRuntime::AdaptiveResearchPressureRuntime(
    const AdaptiveResearchRuntime &kernel,
    const AdaptiveResearchPressureCatalog &catalog)
    : storage_(std::make_unique<Storage>(kernel, catalog)) {}
AdaptiveResearchPressureRuntime::~AdaptiveResearchPressureRuntime() = default;
AdaptiveResearchPressureRuntime::AdaptiveResearchPressureRuntime(
    AdaptiveResearchPressureRuntime &&) noexcept = default;
AdaptiveResearchPressureRuntime &AdaptiveResearchPressureRuntime::operator=(
    AdaptiveResearchPressureRuntime &&) noexcept = default;
const AdaptiveResearchPressureState &
AdaptiveResearchPressureRuntime::get_support_state(
    const AdaptiveResearchCivilizationState &state) const {
  return storage_->states.get_or_create(state);
}
void AdaptiveResearchPressureRuntime::report_metric_signal(
    AdaptiveResearchCivilizationState &state, std::string_view signal_id_view,
    double normalized_value) const {
  const std::string signal_id(signal_id_view);
  if (blank(signal_id))
    throw std::invalid_argument(
        "The value cannot be an empty string or composed entirely of "
        "whitespace. (Parameter 'signalId')");
  const auto pressures =
      storage_->catalog->pressures_for_metric_signal(signal_id);
  if (pressures.empty())
    throw std::invalid_argument("Unknown Research Pressure metric signal '" +
                                signal_id + "'. (Parameter 'signalId')");
  if (normalized_value < 0 || normalized_value > 1 ||
      std::isnan(normalized_value) || std::isinf(normalized_value))
    throw std::out_of_range(
        "Metric signal must be in 0..1. (Parameter 'normalizedValue')\r\n"
        "Actual value was " +
        dotnet_number(normalized_value) + ".");
  auto &support = storage_->states.get_or_create(state);
  AdaptiveResearchPressureState::Writer::set_metric(support, signal_id,
                                                    normalized_value);
  if (normalized_value > 0)
    for (const auto &pressure : pressures)
      AdaptiveResearchPressureState::Writer::activate(support, pressure);
}
void AdaptiveResearchPressureRuntime::report_metric_signals(
    AdaptiveResearchCivilizationState &state,
    std::span<const ResearchPressureMetricSignalEntry> signals_view) const {
  const std::vector<ResearchPressureMetricSignalEntry> signals(
      signals_view.begin(), signals_view.end());
  for (const auto &signal : signals)
    report_metric_signal(state, signal.signal_id, signal.value);
}
std::vector<AdaptiveResearchRuntimeEvent>
AdaptiveResearchPressureRuntime::report_event_signal(
    AdaptiveResearchCivilizationState &state,
    std::string_view event_signal_id_view, double normalized_severity,
    std::optional<std::string_view> context_view) const {
  const std::string event_signal_id(event_signal_id_view);
  const auto context =
      context_view ? std::optional<std::string>(*context_view) : std::nullopt;
  if (blank(event_signal_id))
    throw std::invalid_argument(
        "The value cannot be an empty string or composed entirely of "
        "whitespace. (Parameter 'eventSignalId')");
  const auto pressures =
      storage_->catalog->pressures_for_event_signal(event_signal_id);
  if (pressures.empty())
    throw std::invalid_argument("Unknown Research Pressure event signal '" +
                                event_signal_id +
                                "'. (Parameter 'eventSignalId')");
  if (normalized_severity < 0 || normalized_severity > 1 ||
      std::isnan(normalized_severity) || std::isinf(normalized_severity))
    throw std::out_of_range(
        "Event severity must be in 0..1. (Parameter 'normalizedSeverity')\r\n"
        "Actual value was " +
        dotnet_number(normalized_severity) + ".");
  if (normalized_severity <= 0)
    return {};
  auto &support = storage_->states.get_or_create(state);
  std::vector<AdaptiveResearchRuntimeEvent> events;
  for (const auto &pressure_id : pressures) {
    AdaptiveResearchPressureState::Writer::activate(support, pressure_id);
    const auto &rule = storage_->catalog->get_rule(pressure_id);
    const auto pulse =
        storage_->catalog->runtime_policy().event_pulse_base_points *
        normalized_severity;
    const auto next = clamp(
        dotnet_max(rule.memory_floor, state.get_pressure(pressure_id) + pulse));
    auto produced = storage_->kernel->set_pressure(
        state, pressure_id, next,
        context ? std::optional<std::string_view>(*context) : std::nullopt);
    events.insert(events.end(), produced.begin(), produced.end());
  }
  return events;
}
std::vector<AdaptiveResearchRuntimeEvent>
AdaptiveResearchPressureRuntime::advance(
    AdaptiveResearchCivilizationState &state, double elapsed_years,
    std::optional<std::string_view> context_view) const {
  const auto context =
      context_view ? std::optional<std::string>(*context_view) : std::nullopt;
  if (elapsed_years <= 0 || std::isnan(elapsed_years) ||
      std::isinf(elapsed_years))
    return {};
  auto &support = storage_->states.get_or_create(state);
  const std::vector<std::string> active(support.active_pressure_ids().begin(),
                                        support.active_pressure_ids().end());
  std::vector<AdaptiveResearchRuntimeEvent> events;
  for (const auto &pressure_id : active) {
    const auto &rule = storage_->catalog->get_rule(pressure_id);
    const auto target = storage_->metric_target(support, rule);
    const auto current = state.get_pressure(pressure_id);
    double next{};
    if (target > current)
      next = std::min(
          target,
          current +
              storage_->catalog->runtime_policy().rise_toward_target_per_year *
                  elapsed_years);
    else {
      const auto floor = dotnet_max(target, rule.memory_floor);
      next = dotnet_max(floor, current - rule.decay_per_year * elapsed_years);
    }
    next = clamp(next);
    if (std::abs(next - current) > .000001) {
      auto produced = storage_->kernel->set_pressure(
          state, pressure_id, next,
          context ? std::optional<std::string_view>(*context) : std::nullopt);
      events.insert(events.end(), produced.begin(), produced.end());
    }
    const auto has_support =
        std::ranges::any_of(rule.metric_signal_ids, [&](const auto &signal) {
          return support.get_metric_signal(signal) > 0;
        });
    if (!has_support && next <= 0 && rule.memory_floor <= 0)
      AdaptiveResearchPressureState::Writer::deactivate(support, pressure_id);
  }
  return events;
}
double AdaptiveResearchPressureRuntime::get_metric_target(
    const AdaptiveResearchCivilizationState &state,
    std::string_view pressure_id_view) const {
  const std::string pressure_id(pressure_id_view);
  const ResearchPressureRuleDefinition *rule{};
  try {
    rule = &storage_->catalog->get_rule(pressure_id);
  } catch (const std::out_of_range &) {
    throw std::invalid_argument("Unknown Research Pressure '" + pressure_id +
                                "'. (Parameter 'pressureId')");
  }
  return storage_->metric_target(storage_->states.get_or_create(state), *rule);
}

AdaptiveResearchPressureState &detail::AdaptiveResearchPressureSupportAccess::state(
    const AdaptiveResearchPressureRuntime &runtime,
    const AdaptiveResearchCivilizationState &civilization) {
  return runtime.storage_->states.get_or_create(civilization);
}

} // namespace stellar::core

