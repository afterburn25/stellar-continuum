#define main fleet_state_helper_main
#include "fleet_state_tests.cpp"
#undef main

#include <stellar/core/detail/adaptive_research_sha256.hpp>
#include <stellar/core/fleet_persistence.hpp>

#include <filesystem>
#include <fstream>
#include <sstream>

namespace {
using namespace stellar::core;

FleetCombatSaveDto dto_combat(const Json &v) {
  const auto c = parse_combat(v);
  return {c.profile_id, c.shields, c.armor, c.hull,
          c.weapon_cooldown_remaining_days, c.order, c.target_fleet_id,
          c.defend_system_id, c.retreat_progress_days, c.retreat_started,
          c.is_disengaged, c.disengaged_system_id};
}
FleetSaveDto dto(const Json &v) {
  FleetSaveDto d;
  d.id=v.at("Id"); d.civilization_id=v.at("CivilizationId"); d.name=v.at("Name");
  d.role=static_cast<FleetRole>(v.at("Role").get<int>()); d.design_id=optional_value<std::string>(v.at("DesignId"));
  d.x=static_cast<float>(number(v.at("X"))); d.y=static_cast<float>(number(v.at("Y")));
  d.current_system_id=optional_value<int>(v.at("CurrentSystemId")); d.destination_system_id=optional_value<int>(v.at("DestinationSystemId"));
  d.transit_phase=static_cast<FleetTransitPhase>(v.at("TransitPhase").get<int>());
  d.transit_origin_system_id=optional_value<int>(v.at("TransitOriginSystemId")); d.transit_target_system_id=optional_value<int>(v.at("TransitTargetSystemId"));
  d.transit_progress=number(v.at("TransitProgress"));
  d.local_transit_start_x=static_cast<float>(number(v.at("LocalTransitStartX"))); d.local_transit_start_y=static_cast<float>(number(v.at("LocalTransitStartY")));
  d.local_transit_position_x=static_cast<float>(number(v.at("LocalTransitPositionX"))); d.local_transit_position_y=static_cast<float>(number(v.at("LocalTransitPositionY")));
  d.local_transit_target_x=static_cast<float>(number(v.at("LocalTransitTargetX"))); d.local_transit_target_y=static_cast<float>(number(v.at("LocalTransitTargetY")));
  if(!v.at("PlannedRouteSystemIds").is_null()) d.planned_route_system_ids=v.at("PlannedRouteSystemIds").get<std::vector<int>>();
  d.hold_requested=v.at("HoldRequested"); d.return_to_base_requested=v.at("ReturnToBaseRequested"); d.return_to_base_failure_reason=optional_value<std::string>(v.at("ReturnToBaseFailureReason")); d.mission_order_revision=v.at("MissionOrderRevision");
  d.destination_planetary_body_id=optional_value<int>(v.at("DestinationPlanetaryBodyId")); d.prevent_automatic_settlement=v.at("PreventAutomaticSettlement"); d.settlement_body_id=optional_value<int>(v.at("SettlementBodyId")); d.settlement_days_completed=number(v.at("SettlementDaysCompleted"));
  d.reconnaissance_system_id=optional_value<int>(v.at("ReconnaissanceSystemId")); d.reconnaissance_days_completed=number(v.at("ReconnaissanceDaysCompleted")); d.freight_target_outpost_id=optional_value<int>(v.at("FreightTargetOutpostId")); d.freight_home_colony_id=optional_value<int>(v.at("FreightHomeColonyId"));
  d.cargo_material_capacity=number(v.at("CargoMaterialCapacity")); d.cargo_materials=number(v.at("CargoMaterials")); d.strategic_speed=number(v.at("StrategicSpeed")); d.maximum_leg_range_light_years=number(v.at("MaximumLegRangeLightYears")); d.fuel_capacity_light_years=number(v.at("FuelCapacityLightYears"));
  if(!v.at("FuelRemainingLightYears").is_null()) d.fuel_remaining_light_years=number(v.at("FuelRemainingLightYears"));
  d.sensor_range=static_cast<float>(number(v.at("SensorRange"))); d.is_active=v.at("IsActive");
  if(!v.at("EmbarkedPopulationMillions").is_null()) d.embarked_population_millions=number(v.at("EmbarkedPopulationMillions"));
  d.embarked_population_species_id=optional_value<std::string>(v.at("EmbarkedPopulationSpeciesId"));
  if(!v.at("Combat").is_null()) d.combat=dto_combat(v.at("Combat"));
  if(!v.at("TacticalLoadout").is_null()) d.tactical_loadout=parse_loadout(v.at("TacticalLoadout"));
  if(!v.at("TacticalVessel").is_null()) d.tactical_vessel=parse_vessel(v.at("TacticalVessel"));
  return d;
}
void check_optional_number(const std::optional<double>&a,const Json&e,const std::string&f){ if(e.is_null())check(!a,f); else {check(a.has_value(),f);equal_number(*a,number(e),f);} }
void check_dto(const FleetSaveDto&a,const Json&e,const std::string&f){
  check(a.id==e.at("Id").get<int>()&&a.civilization_id==e.at("CivilizationId").get<int>()&&a.name==e.at("Name").get<std::string>()&&static_cast<int>(a.role)==e.at("Role").get<int>(),f+" identity");
  check(a.design_id==optional_value<std::string>(e.at("DesignId")),f+" design");
#define N(member,Field) equal_number(a.member,number(e.at(Field)),f+"." Field)
  N(x,"X");N(y,"Y");N(transit_progress,"TransitProgress");N(local_transit_start_x,"LocalTransitStartX");N(local_transit_start_y,"LocalTransitStartY");N(local_transit_position_x,"LocalTransitPositionX");N(local_transit_position_y,"LocalTransitPositionY");N(local_transit_target_x,"LocalTransitTargetX");N(local_transit_target_y,"LocalTransitTargetY");
  N(settlement_days_completed,"SettlementDaysCompleted");N(reconnaissance_days_completed,"ReconnaissanceDaysCompleted");N(cargo_material_capacity,"CargoMaterialCapacity");N(cargo_materials,"CargoMaterials");N(strategic_speed,"StrategicSpeed");N(maximum_leg_range_light_years,"MaximumLegRangeLightYears");N(fuel_capacity_light_years,"FuelCapacityLightYears");N(sensor_range,"SensorRange");
#undef N
#define O(member,Field) check(a.member==optional_value<int>(e.at(Field)),f+"." Field)
  O(current_system_id,"CurrentSystemId");O(destination_system_id,"DestinationSystemId");O(transit_origin_system_id,"TransitOriginSystemId");O(transit_target_system_id,"TransitTargetSystemId");O(destination_planetary_body_id,"DestinationPlanetaryBodyId");O(settlement_body_id,"SettlementBodyId");O(reconnaissance_system_id,"ReconnaissanceSystemId");O(freight_target_outpost_id,"FreightTargetOutpostId");O(freight_home_colony_id,"FreightHomeColonyId");
#undef O
  check(static_cast<int>(a.transit_phase)==e.at("TransitPhase").get<int>(),f+" phase");
  if(e.at("PlannedRouteSystemIds").is_null())check(!a.planned_route_system_ids,f+" route null");else check(a.planned_route_system_ids==std::optional(e.at("PlannedRouteSystemIds").get<std::vector<int>>()),f+" route");
  check(a.hold_requested==e.at("HoldRequested").get<bool>()&&a.return_to_base_requested==e.at("ReturnToBaseRequested").get<bool>()&&a.mission_order_revision==e.at("MissionOrderRevision").get<int>()&&a.prevent_automatic_settlement==e.at("PreventAutomaticSettlement").get<bool>()&&a.is_active==e.at("IsActive").get<bool>(),f+" flags");
  check(a.return_to_base_failure_reason==optional_value<std::string>(e.at("ReturnToBaseFailureReason")),f+" return reason");
  check_optional_number(a.fuel_remaining_light_years,e.at("FuelRemainingLightYears"),f+" fuel remaining");check_optional_number(a.embarked_population_millions,e.at("EmbarkedPopulationMillions"),f+" population");
  check(a.embarked_population_species_id==optional_value<std::string>(e.at("EmbarkedPopulationSpeciesId")),f+" species");
  check(a.combat.has_value()==!e.at("Combat").is_null(),f+" combat presence");if(a.combat){FleetCombatState c{a.combat->profile_id,a.combat->shields,a.combat->armor,a.combat->hull,a.combat->weapon_cooldown_remaining_days,a.combat->order,a.combat->target_fleet_id,a.combat->defend_system_id,a.combat->retreat_progress_days,a.combat->retreat_started,a.combat->is_disengaged,a.combat->disengaged_system_id};check_combat(c,e.at("Combat"),f+" combat");}
  const bool has_loadout=e.contains("TacticalLoadout")&&!e.at("TacticalLoadout").is_null();
  check(a.tactical_loadout.has_value()==has_loadout,f+" loadout presence");if(a.tactical_loadout)check_loadout(*a.tactical_loadout,e.at("TacticalLoadout"),f+" loadout");
  const bool has_vessel=e.contains("TacticalVessel")&&!e.at("TacticalVessel").is_null();
  check(a.tactical_vessel.has_value()==has_vessel,f+" vessel presence");if(a.tactical_vessel)check_vessel(*a.tactical_vessel,e.at("TacticalVessel"),f+" vessel");
}
std::string bytes(const std::filesystem::path&p){std::ifstream s(p,std::ios::binary);check(bool(s),"cannot open "+p.string());return {std::istreambuf_iterator<char>(s),{}};}
std::string hash(const std::string&s){auto d=detail::adaptive_research_sha256(std::span(reinterpret_cast<const std::uint8_t*>(s.data()),s.size()));constexpr char x[]="0123456789ABCDEF";std::string r;for(auto b:d){r+=x[b>>4];r+=x[b&15];}return r;}
}

int main(int argc,char**argv){try{
  check(argc==3,"Expected source root and fixture path.");auto source=std::filesystem::absolute(argv[1]);auto path=std::filesystem::absolute(argv[2]);auto raw=bytes(path);check(hash(raw)=="B770FFC79F4214588854CBF857219BC4D7E43FCB9A9FCAB55F3F7B7E4E997F63","fixture hash");auto root=Json::parse(raw);check(root.at("Schema").get<std::string>()=="stellar-fleet-persistence-v1","schema");check(root.at("RowCount").get<std::size_t>()==root.at("Rows").size(),"row count");for(auto&[p,h]:root.at("SourceHashes").items())check(hash(bytes(source/p))==h.get<std::string>(),"source hash "+p);
  size_t count=0;for(const auto&r:root.at("Rows")){
    auto name=r.at("Name").get<std::string>();
    auto operation=r.at("Operation").get<std::string>();
    check(operation=="Restore"||operation=="Capture",name+" operation");
    const int version=r.at("Version").get<int>();
    const bool legacy=r.at("RestoreLegacyPopulation").get<bool>();
    std::optional<std::string>error;
    std::vector<Civilization> civs;for(const auto&c:r.at("Civilizations")){Civilization v;v.id=c.at("Id");v.species_id=c.at("SpeciesId");civs.push_back(v);}
    if(operation=="Restore"){
      std::vector<FleetSaveDto> input;for(const auto&j:r.at("BeforeInput"))input.push_back(dto(j));
      for(size_t i=0;i<input.size();++i)check_dto(input[i],r.at("BeforeInput")[i],name+" decoded before");
      std::optional<std::vector<FleetState>> out;try{out=restore_fleet_dtos(input,civs,version,legacy);}catch(const std::exception&e){error=e.what();}
      for(size_t i=0;i<input.size();++i)check_dto(input[i],r.at("AfterInput")[i],name+" input");
      if(out){check(r.at("Error").is_null(),name+" unexpected success");check(out->size()==r.at("Result").size(),name+" result count");for(size_t i=0;i<out->size();++i)check_fleet((*out)[i],r.at("Result")[i],name+" result");}
    }else{
      std::vector<FleetState> fleets;for(const auto&j:r.at("BeforeInput"))fleets.push_back(parse_fleet(j));
      for(size_t i=0;i<fleets.size();++i)check_fleet(fleets[i],r.at("BeforeInput")[i],name+" decoded before");
      std::optional<std::vector<FleetSaveDto>>out;try{out=capture_fleet_dtos(fleets);}catch(const std::exception&e){error=e.what();}
      check(fleets.size()==r.at("AfterInput").size(),name+" after count");for(size_t i=0;i<fleets.size();++i)check_fleet(fleets[i],r.at("AfterInput")[i],name+" after");
      if(out){check(r.at("Error").is_null(),name+" unexpected success");check(out->size()==r.at("Result").size(),name+" result count");for(size_t i=0;i<out->size();++i)check_dto((*out)[i],r.at("Result")[i],name+" result");}
    }
    if(!r.at("Error").is_null()){
      check(r.at("Error").at("Type").get<std::string>()=="InvalidDataException",name+" source error type");
      check(error.has_value(),name+" expected error");check(*error==r.at("Error").at("Message").get<std::string>(),name+" error");
    }else check(!error,name+" unexpected error");++count;
  }
  for(auto&[p,h]:root.at("SourceHashes").items())check(hash(bytes(source/p))==h.get<std::string>(),"source changed "+p);
  check(count==24,"exact row accounting");
  auto owner_row=root.at("Rows")[18];
  auto live=parse_fleet(owner_row.at("BeforeInput")[0]);
  auto retained=capture_fleet_dtos(std::span(&live,1));
  live.planned_route_system_ids.push_back(999);
  live.tactical_loadout->weapons[0].id="mutated weapon";
  live.tactical_loadout->modules[0].id="mutated module";
  live.tactical_vessel->name="mutated vessel";
  check_dto(retained[0],owner_row.at("Result")[0],"detached ownership");
  std::cout<<"fleet persistence parity: "<<count<<" actual-source rows passed; 1 native ownership probe\n";return 0;
}catch(const std::exception&e){std::cerr<<"fleet persistence parity failure: "<<typeid(e).name()<<": "<<e.what()<<"\ncwd: "<<std::filesystem::current_path().string()<<"\nsource root: "<<(argc>=2?std::filesystem::absolute(argv[1]).string():"<missing>")<<"\nfixture: "<<(argc>=3?std::filesystem::absolute(argv[2]).string():"<missing>")<<"\n";return 1;}}
