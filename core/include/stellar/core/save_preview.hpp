#pragma once

#include <stellar/engine/save_integrity.hpp>

#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>

namespace stellar::core {

// Lightweight top-level metadata for save-slot UIs. Reads the JSON envelope
// without building any campaign runtime, so it stays cheap enough to scan a
// directory of saves. Both Player (flat v17 root) and Developer (envelope
// with a nested Campaign) documents are recognized.
struct PlayerCampaignPreview {
  std::filesystem::path path;
  int format_version{};
  std::optional<int> galaxy_format_version; // absent or JSON null
  std::string game_version;
  std::string saved_at_utc;
  double simulation_days{};
  bool developer{};
  bool tools_used{};
  std::uintmax_t size_bytes{};
  stellar::engine::IntegrityStatus integrity{};
};

// Returns std::nullopt when the file is missing, unreadable, or not a
// recognizable campaign document. Never throws for corrupt JSON — the
// preview is a diagnostic surface, not the loader.
[[nodiscard]] std::optional<PlayerCampaignPreview>
read_player_campaign_preview(const std::filesystem::path &path);

} // namespace stellar::core
