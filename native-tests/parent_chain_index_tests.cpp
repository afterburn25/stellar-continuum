#include <stellar/engine/parent_chain_index.hpp>

#include <iostream>
#include <unordered_set>

using stellar::engine::ParentChainIndex;
using Parents = std::vector<std::optional<std::size_t>>;
namespace {
void require(bool value, const char* message) {
  if (!value) throw std::runtime_error(message);
}
bool walk_reaches_cycle(const Parents& parents, std::size_t start) {
  std::unordered_set<std::size_t> visited;
  std::optional<std::size_t> cursor = start;
  while (cursor) {
    if (!visited.insert(*cursor).second) return true;
    cursor = parents.at(*cursor);
  }
  return false;
}
}

int main() try {
  // Compare every possible graph of up to five nodes with independent walks.
  // Covers self loops, joined branches, tails into cycles, disconnected roots,
  // forward/backward references, and all permutations of node order.
  std::size_t checked = 0;
  for (std::size_t count = 0; count <= 5; ++count) {
    std::size_t combinations = 1;
    for (std::size_t i = 0; i < count; ++i) combinations *= count + 1;
    for (std::size_t code = 0; code < combinations; ++code) {
      Parents parents(count);
      auto digits = code;
      for (auto& parent : parents) {
        const auto digit = digits % (count + 1);
        digits /= count + 1;
        if (digit != count) parent = digit;
      }
      const ParentChainIndex index(parents);
      require(index.size() == count, "Incorrect node count");
      for (std::size_t i = 0; i < count; ++i)
        require(index.reaches_cycle(i) == walk_reaches_cycle(parents, i),
                "Cycle classification differs from independent walk");
      ++checked;
    }
  }

  const auto retained = [] {
    Parents parents{1, 0, std::nullopt};
    ParentChainIndex index(parents);
    parents.assign(3, std::nullopt);
    return index;
  }();
  require(retained.reaches_cycle(0) && retained.reaches_cycle(1) &&
          !retained.reaches_cycle(2), "Results borrow mutable input");
  bool invalid_parent = false;
  try { (void)ParentChainIndex(Parents{1}); }
  catch (const std::invalid_argument&) { invalid_parent = true; }
  require(invalid_parent, "Out-of-range parent accepted");
  bool invalid_query = false;
  try { (void)retained.reaches_cycle(3); }
  catch (const std::out_of_range&) { invalid_query = true; }
  require(invalid_query, "Out-of-range query accepted");

  // Deep input must not exhaust the stack or repeatedly walk each full chain.
  Parents deep(250000);
  for (std::size_t i = 0; i + 1 < deep.size(); ++i) deep[i] = i + 1;
  const ParentChainIndex acyclic(deep);
  for (std::size_t i = 0; i < deep.size(); ++i)
    require(!acyclic.reaches_cycle(i), "Deep acyclic chain misclassified");
  deep.back() = deep.size() / 2;
  const ParentChainIndex cyclic(deep);
  for (std::size_t i = 0; i < deep.size(); ++i)
    require(cyclic.reaches_cycle(i), "Deep cycle or its descendant missed");
  require(!acyclic.reaches_cycle(0), "Rebuilding changed earlier analysis");
  std::cout << checked << " exhaustive parent graphs, ownership, bounds and deep chains passed.\n";
  return 0;
} catch (const std::exception& error) {
  std::cerr << error.what() << '\n';
  return 1;
}
