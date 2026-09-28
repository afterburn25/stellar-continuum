#pragma once
#include <stellar/core/fresh_campaign.hpp>
#include <array>
#include <map>

namespace stellar::core {
inline constexpr std::string_view system_background_version="system-background-v2-faint";
enum class BackgroundCategory { Cool,Dense,Bright,Medium,Old,Cluster,Sparse,Plane,Faint,Warm,Count };
inline constexpr int background_category_count=10;
struct BackgroundAsset {std::string id,category,path;BackgroundCategory type{};double density{};bool band{},cluster{};};
const std::vector<BackgroundAsset>& background_assets();
std::string_view background_category_name(BackgroundCategory);
const BackgroundAsset& background_asset(std::string_view);
struct BackgroundEnvironment {
 GalaxyMorphology morphology{GalaxyMorphology::Spiral};PopulationState population{PopulationState::Mature};
 StellarRegion region{StellarRegion::Disk};double x{},y{},radius{},radial_fraction{},stellar_density{},gas_density{},star_formation{},dark_optical_depth{};
 // The current galaxy is planar; no fabricated height is used.
 std::optional<double> disk_height;
 std::vector<std::uint32_t> phenomena;bool nebula_edge{},canonical_sol{};
 bool operator==(const BackgroundEnvironment&)const=default;
};
struct SystemBackgroundProfile {
 int system_id{};std::uint64_t seed{};BackgroundCategory category{};std::string asset_id,blend_asset_id;
 BackgroundEnvironment environment;std::array<double,background_category_count> weights{};
 double exposure{.5},orientation{},crop_x{},crop_y{},blend_strength{},color_temperature{};bool mirror{};
 // Canonical clear-sky presets suppress local gas imagery and absorption in
 // every system consumer without changing the galaxy's phenomenon simulation.
 bool local_nebula{true};
 bool operator==(const SystemBackgroundProfile&)const=default;
};
SystemBackgroundProfile select_system_background(const BackgroundEnvironment&,std::uint64_t galaxy_seed,int system_id,std::size_t neighborhood_rank=0);
// Immutable generation snapshot and bounded lazy profile cache. Rebuild when the
// campaign changes, never when simulation time or graphics settings change.
class SystemBackgroundCatalog {
public:
 void bind(const FreshCampaignState&);
 const SystemBackgroundProfile& profile(int system_id);
 std::vector<std::pair<std::string,int>> region_examples();
 std::size_t cached_profiles()const{return profiles_.size();}
private:
 struct Location{int id{};double x{},y{};StellarRegion region{};std::size_t rank{},neighbors{};bool canonical_sol{};};
 std::int64_t seed_{};double radius_{1},cell_size_{1};
 StellarPopulationOptions population_;std::optional<GalaxyGenerationConfig> configuration_;std::optional<GalaxyPhenomena> phenomena_;
 std::map<int,Location> locations_;std::map<int,SystemBackgroundProfile> profiles_;
};
std::string system_background_diagnostics(const SystemBackgroundProfile&);
}
