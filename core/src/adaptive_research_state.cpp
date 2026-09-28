#include <stellar/core/adaptive_research_expertise.hpp>
#include <stellar/core/adaptive_research_state.hpp>
#include <stellar/core/detail/adaptive_research_state_writer.hpp>
#include <stellar/core/detail/adaptive_research_weak_state_table.hpp>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <limits>
#include <stdexcept>
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
  Value *find(std::string_view key) noexcept {
    const auto found = index_.find(key);
    return found == index_.end() ? nullptr : &entries_[found->second].value;
  }
  const Value *find(std::string_view key) const noexcept {
    const auto found = index_.find(key);
    return found == index_.end() ? nullptr : &entries_[found->second].value;
  }
  bool contains(std::string_view key) const noexcept {
    return index_.contains(key);
  }
  template <class V> bool insert(std::string key, V &&value) {
    if (index_.contains(key))
      return false;
    std::size_t position;
    if (free_.empty()) {
      position = entries_.size();
      entries_.push_back({std::move(key), std::forward<V>(value), true});
    } else {
      position = free_.back();
      free_.pop_back();
      entries_[position] = {std::move(key), std::forward<V>(value), true};
    }
    index_.emplace(entries_[position].key, position);
    return true;
  }
  template <class V> bool set(std::string key, V &&value) {
    if (auto *existing = find(key)) {
      *existing = std::forward<V>(value);
      return false;
    }
    insert(std::move(key), std::forward<V>(value));
    return true;
  }
  bool erase(std::string_view key) {
    const auto found = index_.find(key);
    if (found == index_.end())
      return false;
    auto &entry = entries_[found->second];
    entry.active = false;
    free_.push_back(found->second);
    index_.erase(found);
    return true;
  }
  template <class Function> void each(Function function) const {
    for (const auto &e : entries_)
      if (e.active)
        function(e.key, e.value);
  }
};

class SlotSet {
  SlotMap<bool> values_;
  std::vector<std::string> cache_;

public:
  bool contains(std::string_view value) const noexcept {
    return values_.contains(value);
  }
  bool insert(std::string value) {
    if (!values_.insert(std::move(value), true))
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
  bool equals(std::span<const std::string> other) const noexcept {
    if (cache_.size() != other.size())
      return false;
    for (const auto &v : other)
      if (!contains(v))
        return false;
    return true;
  }
  std::span<const std::string> values() const noexcept { return cache_; }
  void rebuild() {
    cache_.clear();
    values_.each([&](const auto &key, const auto &) { cache_.push_back(key); });
  }
};

struct CapabilityHash {
  std::size_t operator()(const ResearchCapabilityKey &key) const noexcept {
    std::size_t h = std::hash<std::string>{}(key.capability_id);
    if (key.context_id)
      h ^= std::hash<std::string>{}(*key.context_id) + 0x9e3779b9 + (h << 6) +
           (h >> 2);
    else
      h ^= 0x51ed270b;
    return h;
  }
};
template <class T, class Key, class Hash = std::hash<Key>> class ValueSlotSet {
  struct Entry {
    T value;
    bool active{true};
  };
  std::vector<Entry> entries_;
  std::vector<std::size_t> free_;
  std::unordered_map<Key, std::size_t, Hash> index_;

public:
  bool contains(const Key &key) const noexcept { return index_.contains(key); }
  bool insert(Key key, T value) {
    if (index_.contains(key))
      return false;
    std::size_t p;
    if (free_.empty()) {
      p = entries_.size();
      entries_.push_back({std::move(value), true});
    } else {
      p = free_.back();
      free_.pop_back();
      entries_[p] = {std::move(value), true};
    }
    index_.emplace(std::move(key), p);
    return true;
  }
  bool erase(const Key &key) {
    auto f = index_.find(key);
    if (f == index_.end())
      return false;
    entries_[f->second].active = false;
    free_.push_back(f->second);
    index_.erase(f);
    return true;
  }
  template <class F> void each(F f) const {
    for (const auto &e : entries_)
      if (e.active)
        f(e.value);
  }
};

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
  } else if (first >= 0x80)
    return false;
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
bool blank(std::string_view value) {
  if (value.empty())
    return true;
  while (!value.empty())
    if (!consume_dotnet_whitespace(value))
      return false;
  return true;
}
char ascii_lower(char value) noexcept {
  return value >= 'A' && value <= 'Z' ? static_cast<char>(value + ('a' - 'A'))
                                      : value;
}
bool iequal(std::string_view a, std::string_view b) noexcept {
  if (a.size() != b.size())
    return false;
  for (std::size_t i = 0; i < a.size(); ++i)
    if (ascii_lower(a[i]) != ascii_lower(b[i]))
      return false;
  return true;
}
std::vector<std::uint16_t> utf16_units(std::string_view value) {
  std::vector<std::uint16_t> result;
  while (!value.empty()) {
    const auto first = static_cast<unsigned char>(value.front());
    std::uint32_t cp = first;
    std::size_t length = 1;
    if ((first & 0xe0) == 0xc0) {
      cp = first & 0x1f;
      length = 2;
    } else if ((first & 0xf0) == 0xe0) {
      cp = first & 0x0f;
      length = 3;
    } else if ((first & 0xf8) == 0xf0) {
      cp = first & 0x07;
      length = 4;
    } else if (first >= 0x80)
      throw std::invalid_argument(
          "Invalid UTF-8 in Adaptive Research identifier.");
    if (value.size() < length)
      throw std::invalid_argument(
          "Invalid UTF-8 in Adaptive Research identifier.");
    for (std::size_t i = 1; i < length; ++i) {
      const auto c = static_cast<unsigned char>(value[i]);
      if ((c & 0xc0) != 0x80)
        throw std::invalid_argument(
            "Invalid UTF-8 in Adaptive Research identifier.");
      cp = (cp << 6) | (c & 0x3f);
    }
    const std::uint32_t minimum = length == 1   ? 0
                                  : length == 2 ? 0x80
                                  : length == 3 ? 0x800
                                                : 0x10000;
    if (cp < minimum || cp > 0x10ffff || (cp >= 0xd800 && cp <= 0xdfff))
      throw std::invalid_argument(
          "Invalid UTF-8 in Adaptive Research identifier.");
    value.remove_prefix(length);
    if (cp <= 0xffff)
      result.push_back(static_cast<std::uint16_t>(cp));
    else {
      cp -= 0x10000;
      result.push_back(static_cast<std::uint16_t>(0xd800 + (cp >> 10)));
      result.push_back(static_cast<std::uint16_t>(0xdc00 + (cp & 0x3ff)));
    }
  }
  return result;
}
} // namespace

namespace detail {
std::int64_t checked_next_research_state_revision(std::int64_t value) {
  if (value == std::numeric_limits<std::int64_t>::max())
    throw std::overflow_error(
        "Adaptive Research state revision space is exhausted.");
  return value + 1;
}
} // namespace detail

struct AdaptiveResearchCivilizationState::Storage {
  std::shared_ptr<const detail::AdaptiveResearchStateIdentityToken> identity =
      std::make_shared<const detail::AdaptiveResearchStateIdentityToken>();
  std::string civilization_id, directed_stage;
  std::int64_t revision{}, view_revision{};
  double labs{};
  AdaptiveResearchExpertiseState expertise;
  SlotMap<ResearchNodeRuntimeState> nodes;
  SlotMap<double> pressures;
  SlotMap<ResearchEvidenceInstance> evidence;
  SlotMap<SlotSet> evidence_types;
  SlotSet traits;
  SlotMap<SlotSet> contexts;
  ValueSlotSet<ResearchCapabilityKey, ResearchCapabilityKey, CapabilityHash>
      capabilities;
  SlotSet facilities;
  SlotSet deployments;
  SlotMap<ResearchProjectRuntimeState> projects;
  SlotMap<ResearchProjectRuntimeState> cancelled;
  std::vector<ResearchProjectRuntimeState> cancelled_cache;
  std::vector<ResearchNodeRuntimeState> node_cache;
  std::vector<ResearchPressureEntry> pressure_cache;
  std::vector<ResearchEvidenceInstance> evidence_cache;
  std::vector<ResearchCapabilityKey> capability_cache;
  std::vector<ResearchProjectRuntimeState> project_cache;
  void rebuild_nodes() {
    node_cache.clear();
    nodes.each([&](auto &, const auto &v) { node_cache.push_back(v); });
  }
  void rebuild_pressures() {
    pressure_cache.clear();
    pressures.each(
        [&](const auto &k, double v) { pressure_cache.push_back({k, v}); });
  }
  void rebuild_evidence() {
    evidence_cache.clear();
    evidence.each([&](auto &, const auto &v) { evidence_cache.push_back(v); });
  }
  void rebuild_capabilities() {
    capability_cache.clear();
    capabilities.each([&](const auto &v) { capability_cache.push_back(v); });
  }
  void rebuild_projects() {
    project_cache.clear();
    projects.each([&](auto &, const auto &v) { project_cache.push_back(v); });
  }
  void rebuild_cancelled() {
    cancelled_cache.clear();
    cancelled.each([&](auto &, const auto &v) { cancelled_cache.push_back(v); });
  }
  void touch() {
    revision = detail::checked_next_research_state_revision(revision);
    view_revision = detail::checked_next_research_state_revision(view_revision);
  }
};

const std::shared_ptr<const detail::AdaptiveResearchStateIdentityToken> &
detail::AdaptiveResearchStateIdentityAccess::token(
    const AdaptiveResearchCivilizationState &state) noexcept {
  return state.storage_->identity;
}
bool ResearchNodeRuntimeState::counts_as_established_knowledge()
    const noexcept {
  return maturity == ResearchMaturity::mature ||
         (maturity == ResearchMaturity::archived && resolution &&
          (iequal(*resolution, "mature_history") ||
           iequal(*resolution, "superseded")));
}
AdaptiveResearchCivilizationState::AdaptiveResearchCivilizationState(
    std::string civ, std::string stage)
    : storage_(std::make_unique<Storage>()) {
  if (blank(civ))
    throw std::invalid_argument(
        "The value cannot be an empty string or composed entirely of "
        "whitespace. (Parameter 'civilizationId')");
  if (blank(stage))
    throw std::invalid_argument(
        "The value cannot be an empty string or composed entirely of "
        "whitespace. (Parameter 'startingDirectedProgramStageId')");
  storage_->civilization_id = std::move(civ);
  storage_->directed_stage = std::move(stage);
}
AdaptiveResearchCivilizationState::~AdaptiveResearchCivilizationState() =
    default;
AdaptiveResearchCivilizationState::AdaptiveResearchCivilizationState(
    AdaptiveResearchCivilizationState &&) noexcept = default;
AdaptiveResearchCivilizationState &AdaptiveResearchCivilizationState::operator=(
    AdaptiveResearchCivilizationState &&) noexcept = default;
AdaptiveResearchCivilizationState::AdaptiveResearchCivilizationState(
    const AdaptiveResearchCivilizationState &o)
    : storage_(std::make_unique<Storage>(*o.storage_)) {
  storage_->identity =
      std::make_shared<const detail::AdaptiveResearchStateIdentityToken>();
}
AdaptiveResearchCivilizationState &AdaptiveResearchCivilizationState::operator=(
    const AdaptiveResearchCivilizationState &o) {
  if (this != &o) {
    auto replacement = std::make_unique<Storage>(*o.storage_);
    replacement->identity =
        std::make_shared<const detail::AdaptiveResearchStateIdentityToken>();
    storage_ = std::move(replacement);
  }
  return *this;
}
const std::string &
AdaptiveResearchCivilizationState::civilization_id() const noexcept {
  return storage_->civilization_id;
}
std::int64_t AdaptiveResearchCivilizationState::revision() const noexcept {
  return storage_->revision;
}
std::int64_t
AdaptiveResearchCivilizationState::materialized_view_revision() const noexcept {
  return storage_->view_revision;
}
const AdaptiveResearchExpertiseState &
AdaptiveResearchCivilizationState::expertise() const noexcept {
  return storage_->expertise;
}
const std::string &
AdaptiveResearchCivilizationState::directed_program_stage_id() const noexcept {
  return storage_->directed_stage;
}
double AdaptiveResearchCivilizationState::total_effective_research_labs()
    const noexcept {
  return storage_->labs;
}
double
AdaptiveResearchCivilizationState::assigned_effective_labs() const noexcept {
  double v = 0;
  storage_->projects.each([&](auto &, const auto &p) {
    if (!p.paused)
      v += p.assigned_effective_labs;
  });
  return v;
}
double AdaptiveResearchCivilizationState::free_effective_labs() const noexcept {
  const double available = storage_->labs - assigned_effective_labs();
  return std::isnan(available) ? available : std::max(0.0, available);
}
std::span<const ResearchNodeRuntimeState>
AdaptiveResearchCivilizationState::node_states() const noexcept {
  return storage_->node_cache;
}
std::span<const ResearchPressureEntry>
AdaptiveResearchCivilizationState::pressures() const noexcept {
  return storage_->pressure_cache;
}
std::span<const ResearchEvidenceInstance>
AdaptiveResearchCivilizationState::evidence_instances() const noexcept {
  return storage_->evidence_cache;
}
std::span<const std::string>
AdaptiveResearchCivilizationState::civilization_traits() const noexcept {
  return storage_->traits.values();
}
std::span<const ResearchCapabilityKey>
AdaptiveResearchCivilizationState::capabilities() const noexcept {
  return storage_->capability_cache;
}
std::span<const std::string>
AdaptiveResearchCivilizationState::facility_capabilities() const noexcept {
  return storage_->facilities.values();
}
std::span<const std::string>
AdaptiveResearchCivilizationState::enabled_deployment_event_ids()
    const noexcept {
  return storage_->deployments.values();
}
std::span<const ResearchProjectRuntimeState>
AdaptiveResearchCivilizationState::active_projects() const noexcept {
  return storage_->project_cache;
}
std::span<const ResearchProjectRuntimeState>
AdaptiveResearchCivilizationState::cancelled_projects() const noexcept {
  return storage_->cancelled_cache;
}
const ResearchProjectRuntimeState *
AdaptiveResearchCivilizationState::cancelled_project(std::string_view id) const noexcept {
  return storage_->cancelled.find(id);
}
std::vector<ResearchApplicabilityContextSnapshot>
AdaptiveResearchCivilizationState::applicability_contexts() const {
  std::vector<ResearchApplicabilityContextSnapshot> r;
  storage_->contexts.each([&](const auto &k, const auto &v) {
    std::vector<std::pair<std::string, std::vector<std::uint16_t>>> keyed;
    for (const auto &trait : v.values())
      keyed.push_back({trait, utf16_units(trait)});
    std::ranges::sort(
        keyed, {}, &std::pair<std::string, std::vector<std::uint16_t>>::second);
    std::vector<std::string> traits;
    traits.reserve(keyed.size());
    for (auto &item : keyed)
      traits.push_back(std::move(item.first));
    r.push_back({k, std::move(traits)});
  });
  return r;
}
const ResearchNodeRuntimeState *
AdaptiveResearchCivilizationState::try_get_node_state(
    std::string_view id) const noexcept {
  return storage_->nodes.find(id);
}
bool AdaptiveResearchCivilizationState::has_established_knowledge(
    std::string_view id) const noexcept {
  auto *p = try_get_node_state(id);
  return p && p->counts_as_established_knowledge();
}
const double *AdaptiveResearchCivilizationState::try_get_pressure(
    std::string_view id) const noexcept {
  return storage_->pressures.find(id);
}
double AdaptiveResearchCivilizationState::get_pressure(
    std::string_view id) const noexcept {
  auto *p = try_get_pressure(id);
  return p ? *p : 0;
}
bool AdaptiveResearchCivilizationState::has_evidence_type(
    std::string_view id) const noexcept {
  auto *s = storage_->evidence_types.find(id);
  return s && !s->values().empty();
}
bool AdaptiveResearchCivilizationState::has_evidence_type(
    std::string_view id, const std::optional<std::string> &ctx) const noexcept {
  auto *s = storage_->evidence_types.find(id);
  if (!s)
    return false;
  if (!ctx)
    return !s->values().empty();
  for (const auto &eid : s->values()) {
    auto *e = storage_->evidence.find(eid);
    if (e && (!e->context_id || *e->context_id == *ctx))
      return true;
  }
  return false;
}
bool AdaptiveResearchCivilizationState::has_civilization_trait(
    std::string_view id) const noexcept {
  return storage_->traits.contains(id);
}
bool AdaptiveResearchCivilizationState::has_trait(
    std::string_view id) const noexcept {
  return has_civilization_trait(id);
}
bool AdaptiveResearchCivilizationState::has_applicability_trait(
    std::string_view c, std::string_view t) const noexcept {
  auto *s = storage_->contexts.find(c);
  return s && s->contains(t);
}
std::span<const std::string>
AdaptiveResearchCivilizationState::get_applicability_traits(
    std::string_view c) const noexcept {
  auto *s = storage_->contexts.find(c);
  return s ? s->values() : std::span<const std::string>{};
}
bool AdaptiveResearchCivilizationState::has_facility_capability(
    std::string_view id) const noexcept {
  return storage_->facilities.contains(id);
}
bool AdaptiveResearchCivilizationState::has_capability(
    std::string_view id, const std::optional<std::string> &ctx) const noexcept {
  for (const auto &key : storage_->capability_cache)
    if (key.capability_id == id && key.context_id == ctx)
      return true;
  return false;
}
bool AdaptiveResearchCivilizationState::is_deployment_event_enabled(
    std::string_view id) const noexcept {
  return storage_->deployments.contains(id);
}
} // namespace stellar::core

namespace stellar::core::detail {
using State = AdaptiveResearchCivilizationState;
AdaptiveResearchExpertiseState &
AdaptiveResearchStateWriter::expertise(State &s) noexcept {
  return s.storage_->expertise;
}
void AdaptiveResearchStateWriter::set_total_effective_research_labs(State &s,
                                                                    double v) {
  if (v < 0 || !std::isfinite(v))
    throw std::out_of_range("Specified argument was out of the range of valid "
                            "values. (Parameter 'value')");
  if (std::abs(s.storage_->labs - v) < 1e-7)
    return;
  checked_next_research_state_revision(s.storage_->revision);
  checked_next_research_state_revision(s.storage_->view_revision);
  s.storage_->labs = v;
  s.storage_->touch();
}
bool AdaptiveResearchStateWriter::set_pressure(State &s, std::string id,
                                               double v) {
  v = std::clamp(v, 0.0, 100.0);
  auto *old = s.storage_->pressures.find(id);
  if (v <= 0) {
    if (!old)
      return false;
    checked_next_research_state_revision(s.storage_->revision);
    checked_next_research_state_revision(s.storage_->view_revision);
    s.storage_->pressures.erase(id);
    s.storage_->rebuild_pressures();
    s.storage_->touch();
    return true;
  }
  if (old && std::abs(*old - v) < 1e-7)
    return false;
  checked_next_research_state_revision(s.storage_->revision);
  checked_next_research_state_revision(s.storage_->view_revision);
  s.storage_->pressures.set(std::move(id), v);
  s.storage_->rebuild_pressures();
  s.storage_->touch();
  return true;
}
bool AdaptiveResearchStateWriter::add_evidence(State &s,
                                               ResearchEvidenceInstance e) {
  if (s.storage_->evidence.contains(e.evidence_instance_id))
    return false;
  checked_next_research_state_revision(s.storage_->revision);
  checked_next_research_state_revision(s.storage_->view_revision);
  auto id = e.evidence_instance_id, type = e.evidence_type_id;
  s.storage_->evidence.insert(id, std::move(e));
  auto *set = s.storage_->evidence_types.find(type);
  if (!set) {
    s.storage_->evidence_types.insert(type, SlotSet{});
    set = s.storage_->evidence_types.find(type);
  }
  set->insert(id);
  s.storage_->rebuild_evidence();
  s.storage_->touch();
  return true;
}
bool AdaptiveResearchStateWriter::remove_evidence(State &s,
                                                  std::string_view id) {
  auto *e = s.storage_->evidence.find(id);
  if (!e)
    return false;
  checked_next_research_state_revision(s.storage_->revision);
  checked_next_research_state_revision(s.storage_->view_revision);
  auto type = e->evidence_type_id;
  s.storage_->evidence.erase(id);
  auto *set = s.storage_->evidence_types.find(type);
  set->erase(id);
  if (set->values().empty())
    s.storage_->evidence_types.erase(type);
  s.storage_->rebuild_evidence();
  s.storage_->touch();
  return true;
}
bool AdaptiveResearchStateWriter::add_civilization_trait(State &s,
                                                         std::string id) {
  if (s.storage_->traits.contains(id))
    return false;
  checked_next_research_state_revision(s.storage_->revision);
  checked_next_research_state_revision(s.storage_->view_revision);
  s.storage_->traits.insert(std::move(id));
  s.storage_->touch();
  return true;
}
bool AdaptiveResearchStateWriter::remove_civilization_trait(
    State &s, std::string_view id) {
  if (!s.storage_->traits.contains(id))
    return false;
  checked_next_research_state_revision(s.storage_->revision);
  checked_next_research_state_revision(s.storage_->view_revision);
  s.storage_->traits.erase(id);
  s.storage_->touch();
  return true;
}
bool AdaptiveResearchStateWriter::set_applicability_context_traits(
    State &s, std::string c, std::span<const std::string> ids) {
  if (blank(c))
    throw std::invalid_argument(
        "The value cannot be an empty string or composed entirely of "
        "whitespace. (Parameter 'contextId')");
  SlotSet replacement;
  for (const auto &id : ids)
    replacement.insert(id);
  auto *old = s.storage_->contexts.find(c);
  if (old && old->equals(replacement.values()))
    return false;
  checked_next_research_state_revision(s.storage_->revision);
  checked_next_research_state_revision(s.storage_->view_revision);
  if (replacement.values().empty())
    s.storage_->contexts.erase(c);
  else
    s.storage_->contexts.set(std::move(c), std::move(replacement));
  s.storage_->touch();
  return true;
}
bool AdaptiveResearchStateWriter::add_applicability_trait(State &s,
                                                          std::string c,
                                                          std::string t) {
  if (blank(c))
    throw std::invalid_argument(
        "The value cannot be an empty string or composed entirely of "
        "whitespace. (Parameter 'contextId')");
  auto *set = s.storage_->contexts.find(c);
  if (set && set->contains(t))
    return false;
  checked_next_research_state_revision(s.storage_->revision);
  checked_next_research_state_revision(s.storage_->view_revision);
  if (!set) {
    s.storage_->contexts.insert(c, SlotSet{});
    set = s.storage_->contexts.find(c);
  }
  set->insert(std::move(t));
  s.storage_->touch();
  return true;
}
bool AdaptiveResearchStateWriter::remove_applicability_trait(
    State &s, std::string_view c, std::string_view t) {
  auto *set = s.storage_->contexts.find(c);
  if (!set || !set->contains(t))
    return false;
  checked_next_research_state_revision(s.storage_->revision);
  checked_next_research_state_revision(s.storage_->view_revision);
  set->erase(t);
  if (set->values().empty())
    s.storage_->contexts.erase(c);
  s.storage_->touch();
  return true;
}
bool AdaptiveResearchStateWriter::add_capability(State &s,
                                                 ResearchCapabilityKey k) {
  if (s.storage_->capabilities.contains(k))
    return false;
  checked_next_research_state_revision(s.storage_->revision);
  checked_next_research_state_revision(s.storage_->view_revision);
  auto key = k;
  s.storage_->capabilities.insert(std::move(key), std::move(k));
  s.storage_->rebuild_capabilities();
  s.storage_->touch();
  return true;
}
bool AdaptiveResearchStateWriter::remove_capability(
    State &s, const ResearchCapabilityKey &k) {
  if (!s.storage_->capabilities.contains(k))
    return false;
  checked_next_research_state_revision(s.storage_->revision);
  checked_next_research_state_revision(s.storage_->view_revision);
  s.storage_->capabilities.erase(k);
  s.storage_->rebuild_capabilities();
  s.storage_->touch();
  return true;
}
bool AdaptiveResearchStateWriter::add_facility_capability(State &s,
                                                          std::string id) {
  if (s.storage_->facilities.contains(id))
    return false;
  checked_next_research_state_revision(s.storage_->revision);
  checked_next_research_state_revision(s.storage_->view_revision);
  s.storage_->facilities.insert(std::move(id));
  s.storage_->touch();
  return true;
}
bool AdaptiveResearchStateWriter::remove_facility_capability(
    State &s, std::string_view id) {
  if (!s.storage_->facilities.contains(id))
    return false;
  checked_next_research_state_revision(s.storage_->revision);
  checked_next_research_state_revision(s.storage_->view_revision);
  s.storage_->facilities.erase(id);
  s.storage_->touch();
  return true;
}
void AdaptiveResearchStateWriter::set_facility_capabilities(
    State &s, std::span<const std::string> desired_capabilities) {
  SlotSet desired;
  for (const auto &capability : desired_capabilities)
    desired.insert(capability);

  const std::vector<std::string> existing(
      s.storage_->facilities.values().begin(),
      s.storage_->facilities.values().end());
  for (const auto &capability : existing)
    if (!desired.contains(capability))
      remove_facility_capability(s, capability);
  for (const auto &capability : desired.values())
    add_facility_capability(s, capability);
}
bool AdaptiveResearchStateWriter::add_enabled_deployment_event(State &s,
                                                               std::string id) {
  if (s.storage_->deployments.contains(id))
    return false;
  checked_next_research_state_revision(s.storage_->revision);
  checked_next_research_state_revision(s.storage_->view_revision);
  s.storage_->deployments.insert(std::move(id));
  s.storage_->touch();
  return true;
}
void AdaptiveResearchStateWriter::set_directed_program_stage(State &s,
                                                             std::string id) {
  if (s.storage_->directed_stage == id)
    return;
  checked_next_research_state_revision(s.storage_->revision);
  checked_next_research_state_revision(s.storage_->view_revision);
  s.storage_->directed_stage = std::move(id);
  s.storage_->touch();
}
void AdaptiveResearchStateWriter::set_node_state(State &s,
                                                 ResearchNodeRuntimeState n) {
  auto next = checked_next_research_state_revision(s.storage_->revision);
  checked_next_research_state_revision(s.storage_->view_revision);
  n.revision = next;
  auto id = n.node_id;
  s.storage_->nodes.set(std::move(id), std::move(n));
  s.storage_->rebuild_nodes();
  s.storage_->touch();
}
bool AdaptiveResearchStateWriter::remove_node_state(State &s,
                                                    std::string_view id) {
  if (!s.storage_->nodes.contains(id))
    return false;
  checked_next_research_state_revision(s.storage_->revision);
  checked_next_research_state_revision(s.storage_->view_revision);
  s.storage_->nodes.erase(id);
  s.storage_->rebuild_nodes();
  s.storage_->touch();
  return true;
}
void AdaptiveResearchStateWriter::set_project(State &s,
                                              ResearchProjectRuntimeState p) {
  auto next = checked_next_research_state_revision(s.storage_->revision);
  checked_next_research_state_revision(s.storage_->view_revision);
  p.revision = next;
  auto id = p.node_id;
  s.storage_->projects.set(std::move(id), std::move(p));
  s.storage_->rebuild_projects();
  s.storage_->touch();
}
bool AdaptiveResearchStateWriter::remove_project(State &s,
                                                 std::string_view id) {
  if (!s.storage_->projects.contains(id))
    return false;
  checked_next_research_state_revision(s.storage_->revision);
  checked_next_research_state_revision(s.storage_->view_revision);
  s.storage_->projects.erase(id);
  s.storage_->rebuild_projects();
  s.storage_->touch();
  return true;
}
void AdaptiveResearchStateWriter::mark_view_dirty(State &s) {
  s.storage_->view_revision =
      checked_next_research_state_revision(s.storage_->view_revision);
}
void AdaptiveResearchStateWriter::mark_state_changed(State &s) {
  checked_next_research_state_revision(s.storage_->revision);
  checked_next_research_state_revision(s.storage_->view_revision);
  s.storage_->touch();
}
void AdaptiveResearchStateWriter::set_cancelled_project(State &s,
                                                        ResearchProjectRuntimeState p) {
  p.revision = checked_next_research_state_revision(s.storage_->revision);
  checked_next_research_state_revision(s.storage_->view_revision);
  auto id = p.node_id;
  s.storage_->cancelled.set(std::move(id), std::move(p));
  s.storage_->rebuild_cancelled();
  s.storage_->touch();
}
bool AdaptiveResearchStateWriter::remove_cancelled_project(State &s, std::string_view id) {
  if (!s.storage_->cancelled.contains(id)) return false;
  checked_next_research_state_revision(s.storage_->revision);
  checked_next_research_state_revision(s.storage_->view_revision);
  s.storage_->cancelled.erase(id);
  s.storage_->rebuild_cancelled();
  s.storage_->touch();
  return true;
}
} // namespace stellar::core::detail
