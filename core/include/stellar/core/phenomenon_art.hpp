#pragma once
#include <stellar/core/galaxy_phenomena.hpp>

namespace stellar::core {
struct PhenomenonVisualAsset {
  std::string asset_id,filename,source_category,path;
  PhenomenonType functional_type{};
  int variant_index{},width{},height{};
  bool system_view_eligible{},galaxy_view_eligible{},obscuring{};
  double aspect_ratio{};
};
const std::vector<PhenomenonVisualAsset>& phenomenon_art_catalog();
const PhenomenonVisualAsset& phenomenon_art(const GalaxyPhenomena&,const GalaxyPhenomenon&);
const PhenomenonVisualAsset& phenomenon_art_role(std::string_view);
void assign_phenomenon_art(GalaxyPhenomena&,std::uint64_t seed);
std::string phenomenon_art_inventory();
std::string phenomenon_art_usage(const GalaxyPhenomena&);
} // namespace stellar::core
