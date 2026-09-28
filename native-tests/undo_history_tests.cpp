// Coverage for engine::UndoHistory: commit/undo/redo round-trips, redo
// invalidation on new commits, capacity eviction, and empty-stack no-ops.

#include <stellar/engine/undo_history.hpp>

#include <iostream>
#include <stdexcept>
#include <string>

using stellar::engine::UndoHistory;

namespace {

void require(bool condition, const char *message) {
  if (!condition) throw std::runtime_error(message);
}

} // namespace

int main() {
  try {
    // Basic commit/undo/redo round-trip.
    UndoHistory<int> history(8);
    require(!history.can_undo() && !history.can_redo(),
            "fresh history should be empty");
    require(!history.undo(0).has_value(), "empty undo returned a state");

    history.commit(0);
    history.commit(1);
    require(history.undo_depth() == 2, "commits did not accumulate");

    auto state = history.undo(2);
    require(state && *state == 1, "undo did not restore newest commit");
    state = history.undo(*state);
    require(state && *state == 0, "second undo did not restore oldest");
    require(!history.can_undo(), "undo stack should be drained");
    require(history.redo_depth() == 2, "undos did not feed the redo stack");

    state = history.redo(*state);
    require(state && *state == 1, "redo did not re-apply first undo");
    state = history.redo(*state);
    require(state && *state == 2, "redo did not restore live state");
    require(!history.can_redo(), "redo stack should be drained");

    // A new commit after undoing invalidates the redo future.
    UndoHistory<std::string> branching(8);
    branching.commit("a");
    branching.commit("b");
    auto back = branching.undo("c");
    require(back && *back == "b", "undo returned wrong state");
    branching.commit(*back); // diverge: commit "b" before mutating to "d"
    require(!branching.can_redo(), "new commit must clear the redo stack");

    // Capacity evicts oldest entries; recent history stays reachable.
    UndoHistory<int> capped(3);
    for (int i = 0; i < 10; ++i) capped.commit(i);
    require(capped.undo_depth() == 3, "capacity bound not enforced");
    auto oldest = capped.undo(10);
    require(oldest && *oldest == 9, "newest entry should undo first");
    oldest = capped.undo(*oldest);
    oldest = capped.undo(*oldest);
    require(oldest && *oldest == 7, "oldest retained entry should be 7");
    require(!capped.can_undo(), "evicted entries should stay evicted");

    // clear() empties both stacks.
    capped.clear();
    require(!capped.can_undo() && !capped.can_redo(),
            "clear() left residue");

    // Capacity zero clamps to one.
    UndoHistory<int> single(0);
    single.commit(1);
    single.commit(2);
    require(single.undo_depth() == 1, "capacity-0 clamp failed");

    std::cout << "Undo history checks passed.\n";
    return 0;
  } catch (const std::exception &error) {
    std::cerr << "Undo history checks failed: " << error.what() << '\n';
    return 1;
  }
}
