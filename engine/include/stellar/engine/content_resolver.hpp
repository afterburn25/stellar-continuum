#pragma once

#include "stellar/engine/asset_registry.hpp"

#include <cstdint>
#include <filesystem>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace stellar::engine {

// Resolves content-relative asset paths ("data/logo.png") for a game host.
// Probes a cooked package manifest first — `Content/runtime.stmanifest`
// beside the executable (packaged layout), then `build/cooked/Content/` under
// the project root (dev layout) — and otherwise falls back to the loose
// source file under `packages/<id>/content/`. The local registry is used
// deliberately instead of the global mount so loose runtime resources (like
// a bundled font) keep resolving beside the executable.
class ContentResolver {
public:
  ContentResolver(std::string package_id, std::filesystem::path project_root,
                  std::filesystem::path exe_dir);

  [[nodiscard]] const std::string &package_id() const { return package_id_; }
  // Cooked manifest record for the content-relative path, or nullptr.
  [[nodiscard]] const AssetRecord *find_cooked(std::string_view relpath) const;
  // Decoded bytes of a cooked chunk; nullopt when uncooked or absent.
  [[nodiscard]] std::optional<std::vector<std::uint8_t>>
  read_cooked(std::string_view relpath, std::size_t chunk = 0) const;
  // Loose source path under packages/<id>/content/ (may not exist).
  [[nodiscard]] std::filesystem::path loose_path(std::string_view relpath) const;
  // The asset's primary bytes — cooked chunk 0 (file-like payloads such as
  // audio) or the whole loose file; nullopt if neither layer serves the
  // path. Multi-chunk payloads (e.g. BC7 mip chains) should use
  // find_cooked + per-chunk reads instead.
  [[nodiscard]] std::optional<std::vector<std::uint8_t>>
  read_bytes(std::string_view relpath) const;
  [[nodiscard]] std::size_t cooked_count() const;

private:
  std::string package_id_;
  std::filesystem::path project_root_;
  std::unique_ptr<AssetRegistry> cooked_;
};

} // namespace stellar::engine
