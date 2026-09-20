#pragma once
#include <stellar/core/fresh_campaign.hpp>
namespace stellar::core {
struct DeveloperCelestialEntry {
  std::string key,name,type_id,type_name,region;
  std::optional<int> system_id;
  double x{},y{},mass_solar{},radius_solar{},luminosity_solar{},safe_approach_au{},destruction_radius_au{},inner_hz_au{},outer_hz_au{};
  bool forced{},rare{},central{};
};
struct DeveloperCelestialCount {
  std::string type_id,type_name;
  int natural{},forced{};
};
struct DeveloperCelestialIndex {
  std::vector<DeveloperCelestialEntry> entries;
  std::vector<DeveloperCelestialCount> counts;
};
// Read-only privileged projection. No discovery is granted and no normal
// observer view is widened. Only marked developer worlds can request it.
[[nodiscard]] DeveloperCelestialIndex build_developer_celestial_index(const FreshCampaignState &);
}
