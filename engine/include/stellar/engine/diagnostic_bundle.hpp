#pragma once
#include <cstddef>
#include <filesystem>
#include <span>
#include <string>
#include <string_view>

namespace stellar::engine {
struct DiagnosticBundleEntry { std::string name,bytes; };
struct DiagnosticBundleLimits {
  std::size_t maximum_entry_bytes{64u*1024u*1024u};
  std::size_t maximum_total_bytes{256u*1024u*1024u};
  std::size_t maximum_entries{128};
};
// Immutable caller-owned snapshots only; no live world access or directory crawl.
// Validates relative archive paths and bounds before creating a unique directory.
// Publishes a complete stored ZIP by renaming its owned partial file.
[[nodiscard]] std::filesystem::path write_diagnostic_bundle(
    const std::filesystem::path &root,std::string_view filename,
    std::span<const DiagnosticBundleEntry>,DiagnosticBundleLimits={});
// Exact bounded read: rejects nonregular files, symlinks and changing size.
[[nodiscard]] std::string read_diagnostic_file(const std::filesystem::path &,std::size_t limit);
}
