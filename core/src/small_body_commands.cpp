#include <stellar/core/small_body_commands.hpp>
#include <algorithm>
#include <cmath>
#include <limits>

namespace stellar::core {
int force_developer_small_body_field(FreshCampaignState& world,int system_id,SmallBodyFieldType type,double epoch,int preferred){
  if(!world.developer_provenance)throw std::invalid_argument("Field spawning requires an isolated developer campaign.");
  if(!std::isfinite(epoch)||epoch<0)throw std::invalid_argument("Invalid field epoch.");
  auto system=std::ranges::find(world.systems,system_id,&StellarSystem::id);
  if(system==world.systems.end())throw std::invalid_argument("System no longer exists.");
  auto fields=system->small_body_fields.value_or(std::vector<SmallBodyField>{});
  if(fields.size()>=32)throw std::invalid_argument("This system has reached its 32-field limit.");
  int id=0;for(const auto& f:fields){if(f.id==std::numeric_limits<int>::max())throw std::invalid_argument("Field identity limit reached.");id=std::max(id,f.id+1);}
  std::vector<PlanetaryBody> local;for(const auto& b:world.bodies)if(b.system_id==system_id)local.push_back(b);
  int parent=-1;
  if(type==SmallBodyFieldType::CrackedCluster||type==SmallBodyFieldType::PlanetaryHalo){
    const auto eligible=[&](const PlanetaryBody& b){return b.kind==PlanetaryBodyKind::Planet&&b.environment.has_solid_surface&&!b.has_pre_warp_civilization&&std::ranges::none_of(world.colonies,[&](const auto& c){return c.planetary_body_id==b.id;});};
    auto body=std::ranges::find_if(local,[&](const auto& b){return b.id==preferred&&eligible(b);});
    if(body==local.end())body=std::ranges::find_if(local,[&](const auto& b){return b.cracked_world&&eligible(b);});
    if(body==local.end())body=std::ranges::find_if(local,eligible);
    if(body==local.end())throw std::invalid_argument("No uninhabited solid planet is available for cracked debris.");
    body->cracked_world=true;parent=body->id;
  }
  auto field=make_small_body_field(*system,local,type,id,static_cast<std::uint64_t>(world.seed)^(std::uint64_t(system_id)+1)*0x9e3779b97f4a7c15ULL^(std::uint64_t(id)+1)*0xd1b54a32d192ed03ULL,parent);
  field.epoch_days=epoch;fields.push_back(std::move(field));
  system->small_body_fields=std::move(fields);
  reconcile_stellar_orbit_clearance(*system,local);
  if(parent>=0){auto& body=*std::ranges::find(world.bodies,parent,&PlanetaryBody::id);body.cracked_world=true;
    if(body.appearance)body.appearance=planet_appearance_for_existing(static_cast<std::uint64_t>(world.seed),body,system->stellar_object?&*system->stellar_object:nullptr);
  }
  world.developer_provenance->tools_used=true;
  return id;
}
std::array<double,small_body_resource_count> harvest_small_body_supply(SmallBodyField& f,std::uint32_t body,double capacity,const std::array<double,small_body_resource_count>& demand){
  if(!std::isfinite(capacity)||capacity<0)throw std::invalid_argument("Invalid supply capacity.");
  for(double d:demand)if(!std::isfinite(d)||d<0)throw std::invalid_argument("Invalid supply demand.");
  auto available=small_body_resources(f,body);double total=0;
  for(std::size_t i=0;i<available.size();++i){available[i]=std::min(available[i],demand[i]);total+=available[i];}
  const double fraction=total>capacity?capacity/total:1.;
  for(std::size_t i=0;i<available.size();++i)if(available[i]>0&&fraction>0)available[i]=extract_small_body_resource(f,body,static_cast<SmallBodyResource>(i),available[i]*fraction);else available[i]=0;
  return available;
}
}
