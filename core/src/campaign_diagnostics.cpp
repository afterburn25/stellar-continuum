#include <stellar/core/campaign_diagnostics.hpp>
#include <stellar/core/campaign_calendar.hpp>
#include <stellar/core/campaign_economy_projection.hpp>
#include <stellar/core/campaign_warfare_projection.hpp>
#include <stellar/core/settlement_body_index.hpp>
#include <cmath>
#include <cstdio>
#include <unordered_set>

namespace stellar::core {
std::vector<stellar::engine::DiagnosticRecord> inspect_campaign_operations(
    const FreshCampaignState &world,std::uint64_t tick,double day,std::size_t maximum){
  using namespace stellar::engine;
  if(maximum<1||maximum>4096)throw std::invalid_argument("Invalid operational finding bound.");
  std::vector<DiagnosticRecord> records;
  for(const auto &fleet:world.fleets){
    if(!fleet.is_active||!fleet.return_to_base_failure_reason||fleet.return_to_base_failure_reason->empty())continue;
    DiagnosticRecord r;r.tick=tick;r.game_date=format_campaign_date(day);r.subsystem="fleet";
    r.event_type="return_route_unavailable";r.severity=DiagnosticSeverity::Warning;
    r.entity_id=fleet.id;r.civilization_id=fleet.civilization_id;r.system_id=fleet.current_system_id;
    r.message=*fleet.return_to_base_failure_reason;
    r.values["fuelLightYears"]=fleet.fuel_remaining_light_years;
    records.push_back(std::move(r));if(records.size()==maximum)break;
  }
  // Sustenance shortfalls: colonies whose food/water demand outruns
  // installed supply over a 30-day horizon. The engine economy-analysis
  // framework does the demand/bottleneck math; the projection adapter
  // reshapes authoritative sustenance state (same functions the economy
  // phase calls) — no rules are duplicated or re-derived here.
  if(records.size()<maximum){
    static const auto catalog=sustenance_economy_catalog();
    const SettlementBodyIndex sustenance_index(world.colonies,world.bodies);
    for(const auto &colony:world.colonies){
      if(records.size()>=maximum)break;
      if(colony.kind!=SettlementKind::Colony)continue;
      for(const auto &d:analyze_colony_sustenance(
             colony,sustenance_index.bodies_for(colony),30.0,catalog)){
        if(!d.bottleneck)continue;
        if(records.size()>=maximum)break;
        DiagnosticRecord r;r.tick=tick;r.game_date=format_campaign_date(day);r.subsystem="colony";
        r.event_type="sustenance_shortfall";r.severity=DiagnosticSeverity::Warning;
        r.entity_id=colony.id;r.civilization_id=colony.civilization_id;r.system_id=colony.system_id;
        char message[160];
        std::snprintf(message,sizeof(message),
                      "%s demand exceeds installed supply (%.0f/day unmet, %.1f days reserve).",
                      d.resource.c_str(),d.unmet_per_day,d.reserve_days);
        r.message=message;
        r.values["demandPerDay"]=d.demand_per_day;
        r.values["supplyPerDay"]=d.supply_per_day;
        r.values["reserveDays"]=d.reserve_days;
        records.push_back(std::move(r));
      }
    }
  }
  // Armed foreign presence: an armed fleet stationed in a system that
  // holds another civilization's colonies. Strength values come from
  // the warfare theater projection (WarfareModel reports over real
  // combat profiles), not a recomputed approximation.
  if(records.size()<maximum){
    std::unordered_map<int,std::unordered_set<int>> system_owners;
    for(const auto &colony:world.colonies){
      if(colony.kind!=SettlementKind::Colony)continue;
      system_owners[colony.system_id].insert(colony.civilization_id);
    }
    if(!system_owners.empty()){
      const auto theater=project_warfare_theater(world.fleets,world.systems);
      for(const auto &fleet:world.fleets){
        if(records.size()>=maximum)break;
        if(!fleet.is_active||!fleet.current_system_id||
           fleet.transit_phase!=FleetTransitPhase::None)continue;
        const auto owners=system_owners.find(*fleet.current_system_id);
        if(owners==system_owners.end()||owners->second.contains(fleet.civilization_id))continue;
        const auto report=theater.report(static_cast<std::uint64_t>(fleet.id));
        if(report.attack<=0.0)continue;
        DiagnosticRecord r;r.tick=tick;r.game_date=format_campaign_date(day);r.subsystem="fleet";
        r.event_type="foreign_armed_presence";r.severity=DiagnosticSeverity::Warning;
        r.entity_id=fleet.id;r.civilization_id=fleet.civilization_id;r.system_id=*fleet.current_system_id;
        char message[192];
        std::snprintf(message,sizeof(message),
                      "Armed foreign fleet stationed in a system held by civilization %d (projected %.0f damage/day).",
                      *owners->second.begin(),report.attack);
        r.message=message;
        r.values["projectedAttackPerDay"]=report.attack;
        r.values["projectedHullPool"]=report.hull;
        r.values["strategicSpeed"]=report.speed;
        records.push_back(std::move(r));
      }
    }
  }
  return records;
}
std::vector<stellar::engine::DiagnosticRecord> campaign_step_diagnostics(
    const IntegratedAdaptiveCampaignStepResult &s,std::uint64_t tick,double day){
  using namespace stellar::engine;std::vector<DiagnosticRecord> records;
  const auto event=[&](std::string subsystem,std::string type,int civilization,std::string message)->DiagnosticRecord&{
    DiagnosticRecord r;r.tick=tick;r.game_date=format_campaign_date(day);r.subsystem=std::move(subsystem);
    r.event_type=std::move(type);r.civilization_id=civilization;r.message=std::move(message);
    records.push_back(std::move(r));return records.back();
  };
  for(const auto &e:s.core.construction_events)event("construction","project_event",e.civilization_id,e.message).values["projectId"]=e.project_id;
  for(const auto &e:s.core.shipbuilding_events){auto &r=event("shipbuilding","shipbuilding_event",e.civilization_id,e.message);r.entity_id=e.fleet_id;r.values["designId"]=e.design_id;}
  for(const auto &e:s.core.exploration_events){auto &r=event("exploration","exploration_event",e.civilization_id,e.message);r.entity_id=e.fleet_id;r.system_id=e.system_id;r.values["typeCode"]=static_cast<std::int64_t>(e.type);}
  for(const auto &e:s.core.colonization_events){auto &r=event("colonization","colony_event",e.civilization_id,e.message);r.entity_id=e.colony_id;r.system_id=e.system_id;r.values["fleetId"]=static_cast<std::int64_t>(e.fleet_id);}
  for(const auto &e:s.research_events){auto &r=event("research",e.is_outcome?"research_outcome":"research_event",e.civilization_id,e.message);r.values["nodeId"]=e.node_id;}
  for(const auto &e:s.core.combat_events){auto &r=event("combat","combat_event",e.actor_civilization_id,e.message);r.entity_id=e.actor_fleet_id;r.system_id=e.system_id;r.values["typeCode"]=static_cast<std::int64_t>(e.type);r.values["hullDamage"]=e.hull_damage;r.detail=DiagnosticDetail::Detailed;}
  if(s.diplomacy.processed_diplomacy_events()){
    auto &r=event("diplomacy","processed_events",-1,"Authoritative diplomacy transitions.");r.civilization_id.reset();
    r.values["firstContactEvents"]=static_cast<std::int64_t>(s.diplomacy.first_contact_events_processed);
    r.values["combatIncidents"]=static_cast<std::int64_t>(s.diplomacy.combat_incidents_processed);
    r.values["maintenanceTransitions"]=static_cast<std::int64_t>(s.diplomacy.maintenance_transitions());
  }
  return records;
}
std::vector<stellar::engine::DiagnosticRecord> inspect_campaign_invariants(
    const FreshCampaignState &w,std::uint64_t tick,double day,std::size_t maximum){
  using namespace stellar::engine;
  if(maximum<1||maximum>4096)throw std::invalid_argument("Invalid invariant finding bound.");
  std::vector<DiagnosticRecord> findings;
  const auto emit=[&](std::string subsystem,std::string type,int id,std::string message){
    if(findings.size()>=maximum)return;
    DiagnosticRecord r;r.tick=tick;r.game_date=format_campaign_date(day);r.subsystem=std::move(subsystem);
    r.event_type=std::move(type);r.message=std::move(message);r.entity_id=id;r.severity=DiagnosticSeverity::Critical;
    findings.push_back(std::move(r));
  };
  const auto ids=[&](const auto &items,auto member,std::string_view subsystem){
    std::unordered_set<int> set;for(const auto &item:items)if(!set.insert(item.*member).second)
      emit(std::string(subsystem),"duplicate_id",item.*member,"Duplicate canonical entity ID.");
    return set;
  };
  const auto systems=ids(w.systems,&StellarSystem::id,"galaxy");
  const auto bodies=ids(w.bodies,&PlanetaryBody::id,"planet");
  const auto civilizations=ids(w.civilizations,&Civilization::id,"civilization");
  (void)ids(w.fleets,&FleetState::id,"fleet");(void)ids(w.colonies,&Colony::id,"colony");
  (void)ids(w.economies,&CivilizationEconomy::civilization_id,"economy");
  const auto positive=[&](double value,std::string_view name,int id,std::string_view subsystem){
    if(!std::isfinite(value)||value< -1e-6)emit(std::string(subsystem),"invalid_nonnegative_value",id,std::string(name)+" is non-finite or negative.");
  };
  for(const auto &s:w.systems)if(!std::isfinite(s.position.x)||!std::isfinite(s.position.y)||
      (s.position.depth_light_years&&!std::isfinite(*s.position.depth_light_years)))
    emit("galaxy","invalid_position",s.id,"System position is not finite.");
  for(const auto &b:w.bodies){
    if(!systems.contains(b.system_id))emit("planet","orphaned_body",b.id,"Planetary body references an absent system.");
    if(b.parent_body_id&&!bodies.contains(*b.parent_body_id))emit("planet","orphaned_parent",b.id,"Moon references an absent parent body.");
    positive(b.radius_earth,"Body radius",b.id,"planet");positive(b.mass_earth,"Body mass",b.id,"planet");
  }
  for(const auto &c:w.colonies){
    if(!systems.contains(c.system_id)||!civilizations.contains(c.civilization_id)||
        (c.planetary_body_id&&!bodies.contains(*c.planetary_body_id)))emit("colony","orphaned_colony",c.id,"Colony has an absent owner, system or body.");
    positive(c.population_millions,"Population",c.id,"colony");positive(c.infrastructure,"Infrastructure",c.id,"colony");
    positive(c.stored_food_population_days_millions,"Food reserve",c.id,"colony");
    positive(c.stored_water_population_days_millions,"Water reserve",c.id,"colony");
    (void)ids(c.surface_buildings,&SurfaceBuilding::id,"construction");
    for(const auto &b:c.surface_buildings){positive(b.industry_progress,"Building progress",b.id,"construction");
      if(!std::isfinite(b.x)||!std::isfinite(b.z))emit("construction","invalid_position",b.id,"Surface building position is not finite.");}
  }
  for(const auto &e:w.economies){
    if(!civilizations.contains(e.civilization_id))emit("economy","orphaned_economy",e.civilization_id,"Economy references an absent civilization.");
    positive(e.credits,"Credits",e.civilization_id,"economy");positive(e.industry,"Industry",e.civilization_id,"economy");positive(e.science,"Science",e.civilization_id,"economy");
  }
  for(const auto &f:w.fleets){
    if(!civilizations.contains(f.civilization_id))emit("fleet","orphaned_fleet",f.id,"Fleet references an absent civilization.");
    for(const auto system:{f.current_system_id,f.destination_system_id,f.transit_origin_system_id,f.transit_target_system_id})
      if(system&&!systems.contains(*system))emit("fleet","orphaned_location",f.id,"Fleet references an absent system.");
    if(!std::isfinite(f.position.x)||!std::isfinite(f.position.y))emit("fleet","invalid_position",f.id,"Fleet position is not finite.");
    positive(f.fuel_remaining_light_years,"Fuel",f.id,"fleet");positive(f.embarked_population_millions,"Embarked population",f.id,"fleet");
    if(f.is_active&&!f.current_system_id&&f.transit_phase!=FleetTransitPhase::InterstellarWarp)
      emit("fleet","missing_location",f.id,"Active non-warping fleet has no system location.");
  }
  if(!civilizations.contains(w.player_civilization_id))emit("civilization","missing_player",w.player_civilization_id,"Player empire ID does not exist.");
  return findings;
}
}
