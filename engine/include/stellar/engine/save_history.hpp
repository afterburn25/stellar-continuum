#pragma once

#include <cstddef>
#include <filesystem>

namespace stellar::engine {

// Rolling save history. Slot 1 is the immediate ".bak" produced by
// write_file_atomically's ReplaceFileW; slots 2..depth are progressively
// older saves stored as "<save>.bak.<slot>". The chain is a sibling-file
// layout, so every slot and its ".integrity" sidecar survive independently.
inline constexpr std::size_t k_default_save_history_depth = 4;

// 1-based slot path: slot 1 -> "<save>.bak", slot k -> "<save>.bak.<k>".
[[nodiscard]] std::filesystem::path
history_slot_path(const std::filesystem::path &save_path, std::size_t slot);

// Shifts slot k -> k+1 for k = depth-1 .. 1, discarding the deepest slot,
// moving each slot's ".integrity" sidecar with it. Call BEFORE
// write_file_atomically so the atomic replace recreates slot 1 from the
// outgoing primary. Skip entirely when the write preserves the existing
// backup — a preserved .bak must stay in slot 1.
// Best-effort: an individual failed move leaves the remaining chain intact.
void rotate_save_history(
    const std::filesystem::path &save_path,
    std::size_t depth = k_default_save_history_depth);

// Writes ".integrity" sidecars for every history slot that currently exists
// (covers a freshly produced .bak and repairs any missing rotated sidecar).
void write_history_sidecars(
    const std::filesystem::path &save_path,
    std::size_t depth = k_default_save_history_depth);

// True when any history slot file exists for the save path.
[[nodiscard]] bool has_save_history(const std::filesystem::path &save_path,
                                    std::size_t depth =
                                        k_default_save_history_depth);

} // namespace stellar::engine
