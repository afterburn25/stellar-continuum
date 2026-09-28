#include <stellar/core/developer_celestial_index.hpp>
#include <stellar/core/stellar_population_profiles.hpp>
#include <algorithm>
#include <stdexcept>
#include <unordered_set>
namespace stellar::core {
DeveloperCelestialIndex build_developer_celestial_index(const FreshCampaignState &world){
  if(!world.developer_provenance)throw std::invalid_argument("Celestial inspection requires a developer campaign.");
  DeveloperCelestialIndex result;
  for(const auto &d:stellar_object_definitions())result.counts.push_back({d.id,d.name});
  const auto &ids=world.developer_provenance->coverage_forced_system_ids;
  const std::unordered_set<int> forced(ids.begin(),ids.end());
  for(const auto &s:world.systems){
    if(!s.stellar_object)continue;
    const auto &p=*s.stellar_object;const auto &d=stellar_object_definition(p.type);
    const bool injected=forced.contains(s.id);auto &counts=result.counts[static_cast<std::size_t>(p.type)];
    if(injected)++counts.forced;else ++counts.natural;
    result.entries.push_back({"system:"+std::to_string(s.id),s.name,d.id,d.name,
      s.stellar_region?std::string(stellar_region_name(*s.stellar_region)):"Measured neighborhood",s.id,s.position.x,s.position.y,
      p.mass_solar,p.radius_solar,p.luminosity_solar,p.safe_approach_au,p.destruction_radius_au,p.inner_hz_au,p.outer_hz_au,
      injected,p.hooks.is_rare_discovery,false});
  }
  std::ranges::sort(result.entries,{},&DeveloperCelestialEntry::key);
  const bool core=world.galactic_core&&world.galactic_core->black_hole;
  result.counts.push_back({"central-smbh","Central Supermassive Black Hole",core?1:0,0});
  if(core){
    const auto &c=*world.galactic_core;const auto &p=*c.black_hole;
    result.entries.push_back({c.landmark_key,"Galactic center","central-smbh","Central Supermassive Black Hole","Galactic center",
        {},c.x,c.y,p.mass_solar,0,0,0,0,0,0,false,true,true});
  }
  return result;
}
}
