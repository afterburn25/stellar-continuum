#include <stellar/engine/save_history.hpp>

#include <stellar/engine/save_integrity.hpp>

#include <system_error>

namespace stellar::engine {

std::filesystem::path
history_slot_path(const std::filesystem::path &save_path,
                  const std::size_t slot) {
  auto result = save_path;
  if (slot <= 1) {
    result += ".bak";
    return result;
  }
  result += ".bak." + std::to_string(slot);
  return result;
}

namespace {

[[nodiscard]] bool regular_file(const std::filesystem::path &path) {
  std::error_code error;
  return std::filesystem::is_regular_file(path, error);
}

// Moves `from` to `to`, replacing any existing destination and moving the
// ".integrity" sidecar along (or deleting a stale destination sidecar when
// the source has none). Best-effort: returns false on failure.
bool move_slot(const std::filesystem::path &from,
               const std::filesystem::path &to) {
  std::error_code error;
  std::filesystem::remove(to, error);
  error.clear();
  std::filesystem::rename(from, to, error);
  if (error) return false;

  const auto from_sidecar = integrity_sidecar_path(from);
  const auto to_sidecar = integrity_sidecar_path(to);
  std::filesystem::remove(to_sidecar, error);
  error.clear();
  if (regular_file(from_sidecar))
    std::filesystem::rename(from_sidecar, to_sidecar, error);
  return true;
}

} // namespace

void rotate_save_history(const std::filesystem::path &save_path,
                         const std::size_t depth) {
  if (depth < 2) return;
  for (std::size_t slot = depth; slot >= 2; --slot) {
    const auto from = history_slot_path(save_path, slot - 1);
    if (!regular_file(from)) continue;
    move_slot(from, history_slot_path(save_path, slot));
  }
}

void write_history_sidecars(const std::filesystem::path &save_path,
                            const std::size_t depth) {
  for (std::size_t slot = 1; slot <= depth; ++slot) {
    const auto candidate = history_slot_path(save_path, slot);
    if (regular_file(candidate))
      write_integrity_sidecar_for_file(candidate);
  }
}

bool has_save_history(const std::filesystem::path &save_path,
                      const std::size_t depth) {
  for (std::size_t slot = 1; slot <= depth; ++slot)
    if (regular_file(history_slot_path(save_path, slot))) return true;
  return false;
}

} // namespace stellar::engine
