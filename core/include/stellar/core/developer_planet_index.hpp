#pragma once
#include <stellar/core/fresh_campaign.hpp>
namespace stellar::core {
struct DeveloperPlanetTypeEntry {
  PlanetClass type{};std::string subclass,name;std::size_t count{},artwork_count{};
  std::optional<PlanetaryBody> example;std::string system_name;
  std::size_t rejected_artwork_count{},compatible_artwork_count{};
};
enum class DeveloperPlanetFilter{All,ImportedArtwork,Habitable};
std::vector<DeveloperPlanetTypeEntry> build_developer_planet_index(const FreshCampaignState&,DeveloperPlanetFilter = DeveloperPlanetFilter::All);
// Validated additive command: never replaces colonies, saved bodies or fields.
// preferred_system is tried first; if unsuitable, tries another real system.
std::pair<int,int> force_developer_planet_type(FreshCampaignState&,PlanetClass,
  std::string_view subclass={},int preferred_system=-1,double epoch_days=0);
void ensure_developer_planet_coverage(FreshCampaignState&);
struct DeveloperGiantLaboratory { StellarSystem system; PlanetaryBody body; };
// Persisted developer controls and appearance produce a validated Core fixture.
// It is outside the gameplay galaxy: no system counts, colonies or metadata change.
DeveloperGiantLaboratory build_developer_giant_test(const FreshCampaignState&);
std::pair<int,int> apply_developer_giant_test(FreshCampaignState&,const DeveloperGiantTestRequest&);
}
