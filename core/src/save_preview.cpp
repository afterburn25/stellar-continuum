#include <stellar/core/save_preview.hpp>

#include <nlohmann/json.hpp>

#include <fstream>
#include <sstream>
#include <type_traits>

namespace stellar::core {
namespace {

using Json = nlohmann::json;

[[nodiscard]] std::optional<std::string>
read_bytes(const std::filesystem::path &path) {
  std::ifstream input(path, std::ios::binary);
  if (!input) return std::nullopt;
  std::ostringstream buffer;
  buffer << input.rdbuf();
  if (input.bad()) return std::nullopt;
  auto bytes = std::move(buffer).str();
  if (bytes.size() >= 3 && static_cast<unsigned char>(bytes[0]) == 0xef &&
      static_cast<unsigned char>(bytes[1]) == 0xbb &&
      static_cast<unsigned char>(bytes[2]) == 0xbf)
    bytes.erase(0, 3);
  return bytes;
}

template <typename T>
void copy_field(const Json &root, const char *name, T &out) {
  const auto it = root.find(name);
  if (it == root.end()) return;
  if constexpr (std::is_same_v<T, int>) {
    if (it->is_number_integer()) out = it->get<int>();
  } else if constexpr (std::is_same_v<T, double>) {
    if (it->is_number()) out = it->get<double>();
  } else if constexpr (std::is_same_v<T, bool>) {
    if (it->is_boolean()) out = it->get<bool>();
  } else {
    if (it->is_string()) out = it->get<std::string>();
  }
}

} // namespace

std::optional<PlayerCampaignPreview>
read_player_campaign_preview(const std::filesystem::path &path) {
  const auto bytes = read_bytes(path);
  if (!bytes) return std::nullopt;

  PlayerCampaignPreview preview;
  preview.path = path;
  preview.size_bytes = bytes->size();
  preview.integrity = stellar::engine::verify_integrity(path);

  try {
    const auto root = Json::parse(*bytes, nullptr, false);
    if (root.is_discarded() || !root.is_object()) return std::nullopt;

    // Developer envelope: { DeveloperFormatVersion, Mode, ToolsUsed,
    // Campaign } — the campaign fields live one level down.
    const Json *campaign = &root;
    if (const auto mode = root.find("Mode");
        mode != root.end() && mode->is_string() &&
        mode->get<std::string>() == "Developer") {
      preview.developer = true;
      copy_field(root, "ToolsUsed", preview.tools_used);
      const auto inner = root.find("Campaign");
      if (inner != root.end() && inner->is_object()) campaign = &*inner;
    }

    copy_field(*campaign, "FormatVersion", preview.format_version);
    if (const auto galaxy = campaign->find("GalaxyFormatVersion");
        galaxy != campaign->end() && galaxy->is_number_integer())
      preview.galaxy_format_version = galaxy->get<int>();
    copy_field(*campaign, "GameVersion", preview.game_version);
    copy_field(*campaign, "SavedAtUtc", preview.saved_at_utc);
    copy_field(*campaign, "SimulationDays", preview.simulation_days);

    // A recognizable campaign document always carries FormatVersion.
    if (campaign->find("FormatVersion") == campaign->end())
      return std::nullopt;
    return preview;
  } catch (...) {
    return std::nullopt;
  }
}

} // namespace stellar::core
