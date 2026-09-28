#pragma once

#include <cstddef>
#include <deque>
#include <optional>
#include <utility>

namespace stellar::engine {

// Bounded snapshot-based undo/redo history for value-semantic document state.
// Callers commit the pre-mutation snapshot before applying a change; undo()
// and redo() return the state to restore while preserving the current one on
// the opposite stack. Snapshots — not deltas — keep restored state
// self-consistent and make the history usable by any tool without a command
// vocabulary. Oldest entries beyond capacity drop away; the combined depth of
// both stacks plus the live state never exceeds capacity + 1.
template <typename T> class UndoHistory {
public:
  explicit UndoHistory(std::size_t capacity = 64)
      : capacity_(capacity == 0 ? 1 : capacity) {}

  [[nodiscard]] std::size_t capacity() const noexcept { return capacity_; }
  [[nodiscard]] bool can_undo() const noexcept { return !undo_.empty(); }
  [[nodiscard]] bool can_redo() const noexcept { return !redo_.empty(); }
  [[nodiscard]] std::size_t undo_depth() const noexcept { return undo_.size(); }
  [[nodiscard]] std::size_t redo_depth() const noexcept { return redo_.size(); }

  // Records `pre_mutation_state` as the state undo() restores. A new commit
  // invalidates the redo future.
  void commit(const T &pre_mutation_state) {
    undo_.push_back(pre_mutation_state);
    while (undo_.size() > capacity_) undo_.pop_front();
    redo_.clear();
  }

  // Returns the newest committed state to restore, preserving `current` on
  // the redo stack. An empty stack returns nullopt and leaves both untouched.
  std::optional<T> undo(const T &current) {
    if (undo_.empty()) return std::nullopt;
    redo_.push_back(current);
    T restored = std::move(undo_.back());
    undo_.pop_back();
    return restored;
  }

  // Returns the newest undone state to re-apply, preserving `current` on the
  // undo stack. An empty stack returns nullopt.
  std::optional<T> redo(const T &current) {
    if (redo_.empty()) return std::nullopt;
    undo_.push_back(current);
    T restored = std::move(redo_.back());
    redo_.pop_back();
    return restored;
  }

  void clear() noexcept {
    undo_.clear();
    redo_.clear();
  }

private:
  std::size_t capacity_;
  std::deque<T> undo_, redo_;
};

} // namespace stellar::engine
