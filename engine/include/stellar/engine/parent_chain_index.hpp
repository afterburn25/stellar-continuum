#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <stdexcept>
#include <vector>

namespace stellar::engine {

// Immutable analysis of a graph where each node has at most one parent.
// A missing parent ends a chain. References must be valid input indices;
// callers decide how absent external identities map to missing parents.
// O(nodes) construction, O(1) queries, one byte per node, no recursion.
// Descendants leading into a cycle are flagged as well as the cycle itself.
class ParentChainIndex final {
public:
  explicit ParentChainIndex(std::span<const std::optional<std::size_t>> parents)
      : states_(parents.size()) {
    for (const auto parent : parents)
      if (parent && *parent >= parents.size())
        throw std::invalid_argument("Parent index is outside the supplied graph.");

    for (std::size_t start = 0; start < parents.size(); ++start) {
      if (states_[start] != unseen) continue;
      std::optional<std::size_t> cursor = start;
      while (cursor && states_[*cursor] == unseen) {
        states_[*cursor] = visiting;
        cursor = parents[*cursor];
      }
      const auto resolved = cursor &&
          (states_[*cursor] == visiting || states_[*cursor] == cyclic)
          ? cyclic : acyclic;
      // Resolve the complete current walk, stopping when it meets an already
      // resolved chain or returns to its newly marked cycle. No path stack.
      cursor = start;
      while (cursor && states_[*cursor] == visiting) {
        states_[*cursor] = resolved;
        cursor = parents[*cursor];
      }
    }
  }

  [[nodiscard]] std::size_t size() const noexcept { return states_.size(); }
  [[nodiscard]] bool reaches_cycle(std::size_t index) const {
    return states_.at(index) == cyclic;
  }

private:
  static constexpr std::uint8_t unseen = 0, visiting = 1, acyclic = 2, cyclic = 3;
  std::vector<std::uint8_t> states_;
};

} // namespace stellar::engine
