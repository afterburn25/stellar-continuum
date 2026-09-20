#include <stellar/core/small_body_fields.hpp>
#include "small_body_configuration_data.hpp"
#include <nlohmann/json.hpp>
#include <numeric>

namespace stellar::core {
SmallBodyConfiguration parse_small_body_configuration(std::string_view text) {
  const auto j=nlohmann::json::parse(text);
  if(j.at("version")!=1)throw std::invalid_argument("Unsupported small-body configuration");
  SmallBodyConfiguration c;
  c.belt_weights=j.at("belt_weights").get<std::array<double,5>>();
  c.rocky_weights=j.at("rocky_weights").get<std::array<double,4>>();
#define READ(key) c.key=j.at(#key).get<double>()
  READ(disk_chance);READ(shattered_chance);READ(young_disk_multiplier);READ(old_shattered_multiplier);
  READ(giant_belt_multiplier);READ(disturbed_multiplier);READ(cracked_chance);READ(local_cluster_chance);
  READ(cracked_multiplier);READ(cracked_shattered_chance);READ(halo_chance);READ(irregular_fraction);
  READ(spin_min);READ(spin_max);READ(orbit_unit);READ(orbit_exponent);READ(planet_scale);READ(moon_scale);
#undef READ
  if(j.at("types").size()!=c.types.size())throw std::invalid_argument("All nine small-body types are required");
  auto weights=[](const auto& w){double total=0;for(double v:w){if(!std::isfinite(v)||v<0)throw std::invalid_argument("Invalid field weights");total+=v;}if(total<=0)throw std::invalid_argument("Empty field weights");};
  weights(c.belt_weights);weights(c.rocky_weights);
  for(std::size_t i=0;i<c.types.size();++i){const auto& t=j.at("types").at(i);auto& r=c.types[i];
#define READ(key) r.key=t.at(#key).get<decltype(r.key)>()
    READ(density_min);READ(density_max);READ(clustering);READ(eccentricity);READ(thickness);READ(arc_fraction);
    READ(visible_count);READ(body_count);r.composition=t.at("composition").get<std::array<double,8>>();
#undef READ
    r.body_pool=static_cast<SmallBodyAssetPool>(t.at("body_pool").get<int>());
    r.dust_pool=static_cast<SmallBodyAssetPool>(t.at("dust_pool").get<int>());
    r.far_pool=static_cast<SmallBodyAssetPool>(t.at("far_pool").get<int>());
    SmallBodyField test;test.inner_radius_au=1;test.outer_radius_au=2;test.thickness_au=r.thickness;
    test.density=r.density_max;test.body_count=r.body_count;test.visible_count=r.visible_count;test.composition=r.composition;
    test.clustering=r.clustering;test.eccentricity=r.eccentricity;test.arc_fraction=r.arc_fraction;
    test.body_pool=r.body_pool;test.dust_pool=r.dust_pool;test.far_pool=r.far_pool;
    validate_small_body_field(test);
    if(!std::isfinite(r.density_min)||r.density_min<0||r.density_min>r.density_max)throw std::invalid_argument("Invalid field density range");
  }
  for(double p:{c.disk_chance,c.shattered_chance,c.cracked_chance,c.local_cluster_chance,c.cracked_shattered_chance,c.halo_chance,c.irregular_fraction})
    if(!std::isfinite(p)||p<0||p>1)throw std::invalid_argument("Invalid field probability");
  for(double m:{c.young_disk_multiplier,c.old_shattered_multiplier,c.giant_belt_multiplier,c.disturbed_multiplier,c.cracked_multiplier,c.planet_scale,c.moon_scale,c.orbit_unit})
    if(!std::isfinite(m)||m<=0)throw std::invalid_argument("Invalid field multiplier");
  if(!std::isfinite(c.spin_min)||!std::isfinite(c.spin_max)||c.spin_min<=0||c.spin_max<c.spin_min||c.orbit_exponent<=0||c.orbit_exponent>1||!std::isfinite(c.orbit_exponent))throw std::invalid_argument("Invalid field motion or presentation");
  return c;
}
const SmallBodyConfiguration& small_body_configuration(){static const auto c=parse_small_body_configuration(small_body_configuration_json);return c;}
std::string_view small_body_field_name(SmallBodyFieldType t){constexpr std::array names{"Rocky asteroid belt","Metallic-rich asteroid belt","Carbonaceous asteroid belt","Mixed asteroid belt","Ice belt","Debris disk","Shattered debris belt","Cracked-world debris cluster","Planetary debris halo"};return names.at(static_cast<std::size_t>(t));}
std::string_view small_body_material_name(SmallBodyMaterial t){constexpr std::array names{"Rock","Metal","Carbonaceous","Water ice","Methane ice","Ammonia ice","Mixed rock / ice","Frozen volatiles"};return names.at(static_cast<std::size_t>(t));}
std::string_view small_body_resource_name(SmallBodyResource t){constexpr std::array names{"Minerals","Metals","Organics","Water","Hydrogen","Deuterium","Volatiles","Salvage","Exotic minerals"};return names.at(static_cast<std::size_t>(t));}
std::string_view small_body_asset_name(SmallBodyAssetPool p){constexpr std::array names{"Isolated rocky asteroid bodies","Isolated icy asteroid bodies","Asteroid dust - particulate layer","Ice dust - frozen particulate layer","Asteroid belt far-view layer","Ice belt far-view layer","Ice cluster layer","Dense debris disk layer","Shattered belt - broken field"};return names.at(static_cast<std::size_t>(p));}
}
