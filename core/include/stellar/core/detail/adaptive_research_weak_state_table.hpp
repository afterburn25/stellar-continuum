#pragma once

#include <stellar/core/adaptive_research_state.hpp>

#include <algorithm>
#include <cstddef>
#include <functional>
#include <map>
#include <memory>
#include <utility>

namespace stellar::core::detail {

struct AdaptiveResearchStateIdentityToken final {};

class AdaptiveResearchStateIdentityAccess final {
public:
  [[nodiscard]] static const std::shared_ptr<
      const AdaptiveResearchStateIdentityToken> &
  token(const AdaptiveResearchCivilizationState &state) noexcept;
};

// Internal ConditionalWeakTable analogue. Keys compare shared ownership rather
// than addresses, so allocator address reuse cannot recover expired support.
// Values have stable addresses and do not retain civilization state.
template <class Value> class AdaptiveResearchWeakStateTable final {
public:
  AdaptiveResearchWeakStateTable() = default;
  AdaptiveResearchWeakStateTable(const AdaptiveResearchWeakStateTable &) =
      delete;
  AdaptiveResearchWeakStateTable &
  operator=(const AdaptiveResearchWeakStateTable &) = delete;
  AdaptiveResearchWeakStateTable(AdaptiveResearchWeakStateTable &&) = delete;
  AdaptiveResearchWeakStateTable &
  operator=(AdaptiveResearchWeakStateTable &&) = delete;

  Value &get_or_create(const AdaptiveResearchCivilizationState &state) {
    return get_or_create(state, [] { return Value{}; });
  }

  template <class Factory>
  Value &get_or_create(const AdaptiveResearchCivilizationState &state,
                       Factory &&factory) {
    maintain();
    const std::weak_ptr<const AdaptiveResearchStateIdentityToken> key(
        AdaptiveResearchStateIdentityAccess::token(state));
    const auto found = entries_.find(key);
    if (found != entries_.end())
      return *found->second;
    auto value =
        std::make_unique<Value>(std::invoke(std::forward<Factory>(factory)));
    auto *result = value.get();
    entries_.emplace(key, std::move(value));
    if (cursor_ == entries_.end())
      cursor_ = entries_.begin();
    return *result;
  }

  [[nodiscard]] Value *try_get(const AdaptiveResearchCivilizationState &state) {
    maintain();
    const std::weak_ptr<const AdaptiveResearchStateIdentityToken> key(
        AdaptiveResearchStateIdentityAccess::token(state));
    const auto found = entries_.find(key);
    return found == entries_.end() ? nullptr : found->second.get();
  }

  [[nodiscard]] const Value *
  try_get(const AdaptiveResearchCivilizationState &state) const {
    const std::weak_ptr<const AdaptiveResearchStateIdentityToken> key(
        AdaptiveResearchStateIdentityAccess::token(state));
    const auto found = entries_.find(key);
    return found == entries_.end() ? nullptr : found->second.get();
  }

  void maintain(std::size_t limit = 8) {
    if (entries_.empty()) {
      cursor_ = entries_.end();
      return;
    }
    if (cursor_ == entries_.end())
      cursor_ = entries_.begin();
    const auto checks = std::min(limit, entries_.size());
    for (std::size_t checked = 0; checked < checks && !entries_.empty();
         ++checked) {
      if (cursor_ == entries_.end())
        cursor_ = entries_.begin();
      const auto current = cursor_++;
      if (current->first.expired())
        entries_.erase(current);
    }
    if (entries_.empty())
      cursor_ = entries_.end();
  }

  [[nodiscard]] std::size_t entry_count_for_testing() const noexcept {
    return entries_.size();
  }

private:
  using Entries = std::map<
      std::weak_ptr<const AdaptiveResearchStateIdentityToken>,
      std::unique_ptr<Value>,
      std::owner_less<std::weak_ptr<const AdaptiveResearchStateIdentityToken>>>;
  Entries entries_;
  typename Entries::iterator cursor_ = entries_.end();
};

} // namespace stellar::core::detail
