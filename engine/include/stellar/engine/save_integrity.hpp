#pragma once

#include <cstdint>
#include <filesystem>
#include <optional>
#include <span>
#include <string>
#include <string_view>

namespace stellar::engine {

// Save-file integrity sidecars. The canonical save bytes are parity-locked,
// so corruption detection rides alongside: `<save>.integrity` holds
// "fnv1a64:<hex>" computed over the exact bytes written. Verification is
// tolerant by design — a missing sidecar (old saves, hand-copied files)
// reports SidecarAbsent, never a failure; a mismatch reports Mismatch so the
// loader can fall back to the .bak copy.

std::uint64_t save_digest(std::span<const std::byte> bytes) noexcept;
// Returns a path (not string) so save directories with non-ANSI characters
// keep their wide name — narrow conversion would throw or mangle them.
std::filesystem::path
integrity_sidecar_path(const std::filesystem::path &save_path);

// Atomically writes the sidecar for `bytes`. Returns false on IO failure —
// a missing sidecar is acceptable by design, so callers log rather than
// throw.
bool write_integrity_sidecar(const std::filesystem::path &save_path,
                             std::span<const std::byte> bytes);
// Convenience: hashes the file's current contents and writes its sidecar
// (used to cover .bak copies produced by atomic writes).
bool write_integrity_sidecar_for_file(
    const std::filesystem::path &save_path);

enum class IntegrityStatus {
  Verified,
  Mismatch,
  SidecarAbsent,
  SaveMissing,
};

IntegrityStatus verify_integrity(const std::filesystem::path &save_path);
std::optional<std::string>
integrity_error(const std::filesystem::path &save_path);

} // namespace stellar::engine
