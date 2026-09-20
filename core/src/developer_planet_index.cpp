#include <stellar/core/developer_planet_index.hpp>
#include <stellar/core/civilization_catalog.hpp>
#include <algorithm>
#include <limits>
#include <map>
#include <cmath>
namespace stellar::core {
std::vector<DeveloperPlanetTypeEntry> build_developer_planet_index(const FreshCampaignState& w,DeveloperPlanetFilter filter){
 if(!w.developer_provenance)throw std::invalid_argument("Planet index requires an isolated developer campaign");
 std::vector<DeveloperPlanetTypeEntry> rows;std::map<std::pair<PlanetClass,std::string>,std::size_t> lookup;
 for(const auto& r:planet_type_registry()){lookup[{r.base_class,r.subclass}]=rows.size();DeveloperPlanetTypeEntry entry;entry.type=r.base_class;entry.subclass=r.subclass;entry.name=r.name;entry.artwork_count=r.accepted_image_pool.size();entry.rejected_artwork_count=r.rejected_image_pool.size();entry.compatible_artwork_count=r.compatible_image_pool.size();rows.push_back(std::move(entry));}
 const auto player=std::ranges::find(w.civilizations,w.player_civilization_id,&Civilization::id);
 for(const auto& b:w.bodies)if(b.appearance){
  const bool imported=!b.appearance->source_asset_id.empty()&&!b.appearance->source_asset_id.starts_with("sol:");
  if(filter==DeveloperPlanetFilter::ImportedArtwork&&!imported)continue;
  if(filter==DeveloperPlanetFilter::Habitable&&(player==w.civilizations.end()||b.has_pre_warp_civilization||b.cracked_world||
     std::ranges::any_of(w.colonies,[&](const auto& c){return c.planetary_body_id==b.id;})||
     !assess_species_planet(species_environment_profile(player->species_id),b).naturally_colonizable))continue;
  auto it=lookup.find({b.appearance->primary_class,b.appearance->subclass});if(it==lookup.end())continue;auto& row=rows[it->second];++row.count;if(!row.example||(imported&&row.example->appearance->source_asset_id.empty()))row.example=b;
 }
 for(auto& row:rows)if(row.example){const auto system=std::ranges::find(w.systems,row.example->system_id,&StellarSystem::id);if(system!=w.systems.end())row.system_name=system->name;}
 return rows;
}
std::pair<int,int> force_developer_planet_type(FreshCampaignState& w,PlanetClass type,std::string_view sub,int preferred,double epoch){
 if(!w.developer_provenance)throw std::invalid_argument("Planet generation requires an isolated developer campaign");
 if(!std::isfinite(epoch)||epoch<0)throw std::invalid_argument("Invalid planet example epoch");
 std::vector<const PlanetSubclassDefinition*> choices;for(const auto& s:planet_subclass_definitions())if(s.generation_enabled&&s.primary==type&&(sub.empty()||s.id==sub))choices.push_back(&s);
 if(choices.empty())throw std::invalid_argument("Unknown requested planet subtype");
 int id=0;std::map<int,int> orbit;std::map<int,int> counts;
 for(const auto& b:w.bodies){if(b.id==std::numeric_limits<int>::max())throw std::length_error("Planet identity space is exhausted");id=std::max(id,b.id+1);orbit[b.system_id]=std::max(orbit[b.system_id],b.orbit_index+1);if(!b.parent_body_id)++counts[b.system_id];}
 std::vector<StellarSystem*> systems;for(auto& s:w.systems)if(s.id!=sol_system_id&&s.stellar_object&&s.stellar_object->luminosity_solar>0&&counts[s.id]<18)systems.push_back(&s);
 std::stable_sort(systems.begin(),systems.end(),[&](auto* a,auto* b){return std::pair{a->id!=preferred,counts[a->id]}<std::pair{b->id!=preferred,counts[b->id]};});
 std::string last="No suitable luminous system has space";
 for(auto* system:systems)for(std::size_t n=0;n<choices.size();++n){const auto& s=*choices[(n+static_cast<std::uint64_t>(w.seed)+id)%choices.size()];
  try{const int host=system->stellar_orbits?system->stellar_orbits->belt_host:0;const auto physics=stellar_host_physics(*system,host);
   auto b=make_planet_type_example(static_cast<std::uint64_t>(w.seed),id,system->id,orbit[system->id],physics,type,s.id);
   if(!stellar_host_accepts_orbit(*system,host,b.stellar_exposure->orbit_au,b.orbital_eccentricity)){last="Requested thermal orbit is outside the stable stellar host region";continue;}
   // Avoid inserting a QA object through an existing primary orbit.
   bool overlap=false;for(const auto& other:w.bodies)if(other.system_id==system->id&&!other.parent_body_id&&other.stellar_exposure&&std::abs(other.stellar_exposure->orbit_au/b.stellar_exposure->orbit_au-1)<.035){overlap=true;break;}if(overlap){last="Requested thermal orbit overlaps another body";continue;}
   auto fields=system->small_body_fields.value_or(std::vector<SmallBodyField>{});
   if(b.cracked_world){if(fields.size()>=32){last="Debris field limit reached";continue;}int field_id=0;for(const auto& f:fields)field_id=std::max(field_id,f.id+1);auto field=make_small_body_field(*system,std::span<const PlanetaryBody>(&b,1),SmallBodyFieldType::CrackedCluster,field_id,b.appearance->visual_seed,b.id);field.epoch_days=epoch;fields.push_back(std::move(field));}
   const auto result=std::pair{b.system_id,b.id};w.bodies.push_back(std::move(b));if(type==PlanetClass::Cracked)system->small_body_fields=std::move(fields);
   if(system->stellar_orbits)system->stellar_orbits->planets.push_back({id,host});
   if(system->small_body_fields)for(auto& field:*system->small_body_fields)place_small_body_field(*system,w.bodies,field);
   reconcile_stellar_orbit_clearance(*system,w.bodies);
   w.developer_provenance->tools_used=true;return result;
  }catch(const std::invalid_argument& e){last=e.what();}
 }
 throw std::invalid_argument("No physically eligible example orbit: "+last);
}
void ensure_developer_planet_coverage(FreshCampaignState& w){
 const auto rows=build_developer_planet_index(w);for(const auto& row:rows)if(!row.count&&planet_subclass_definition(row.type,row.subclass).generation_enabled)force_developer_planet_type(w,row.type,row.subclass);
}
}
