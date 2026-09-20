#pragma once
#include <stellar/core/stellar_object.hpp>
#include <stellar/engine/density_mask.hpp>
#include <optional>
namespace stellar::core {
enum class PopulationSelection { Random, Starburst, Active, Mature, Aging, Quiescent };
inline constexpr std::string_view galaxy_generator_version="galaxy-configuration-v4";
inline constexpr std::string_view galaxy_visual_version="galaxy-art-v1";
struct GalaxyGenerationConfig {
  std::int64_t base_seed{};
  GalaxyMorphology morphology{GalaxyMorphology::Spiral};
  PopulationSelection requested_population{PopulationSelection::Random};
  PopulationState resolved_population{PopulationState::Mature};
  int system_count{500},pre_warp_count{6},ancient_count{1};
  std::string player_species_id{"terran_baseline"};
  bool developer_full_coverage{};
  std::string generator_version{galaxy_generator_version},asset_set_version{galaxy_visual_version};
  std::string fingerprint,preview_asset_id,map_asset_id;
  bool operator==(const GalaxyGenerationConfig&)const=default;
};
struct GalaxyVisualPair {
  std::string preview_id,map_id,preview_path,map_path,diagnostic;
  bool fallback{};
  double aspect{1};
};
struct GalaxyFootprintFrame {double left{},top{},width{},height{};};
std::string_view galaxy_morphology_id(GalaxyMorphology);
std::string_view population_selection_id(PopulationSelection);
std::string_view population_selection_label(PopulationSelection);
PopulationState resolve_population_state(std::int64_t,GalaxyMorphology,PopulationSelection,std::string_view version=galaxy_generator_version);
GalaxyGenerationConfig resolve_galaxy_configuration(GalaxyGenerationConfig);
void validate_galaxy_configuration(const GalaxyGenerationConfig&);
std::uint64_t galaxy_generation_stream(const GalaxyGenerationConfig&);
GalaxyVisualPair galaxy_visual_pair(GalaxyMorphology,std::optional<PopulationState> = {});
const stellar::engine::DensityMask& galaxy_density_mask(std::string_view map_asset_id);
GalaxyFootprintFrame galaxy_footprint_frame(GalaxyMorphology,int system_count,std::optional<PopulationState> = {});
bool galaxy_has_central_black_hole(const GalaxyGenerationConfig&);
std::string galaxy_configuration_description(const GalaxyGenerationConfig&);
}
