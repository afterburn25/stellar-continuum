#include <stellar/core/campaign_diagnostics.hpp>
#include <stellar/core/campaign_calendar.hpp>
#include <stellar/core/campaign_colony_projection.hpp>
#include <stellar/core/campaign_economy.hpp>
#include <stellar/core/campaign_economy_projection.hpp>
#include <stellar/core/campaign_logistics_projection.hpp>
#include <stellar/core/campaign_population_projection.hpp>
#include <stellar/core/campaign_warfare_projection.hpp>
#include <stellar/core/construction_state.hpp>
#include <stellar/core/fleet_reach.hpp>
#include <stellar/core/fleet_state.hpp>
#include <stellar/core/lane_network.hpp>
#include <stellar/core/logistics.hpp>
#include <stellar/core/settlement_body_index.hpp>
#include <stellar/core/surface_economy.hpp>
#include <stellar/engine/strategic_ai.hpp>
#include <cmath>
#include <cstdio>
#include <unordered_map>
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
        r.event_type=d.resource=="res.power"?"power_shortfall":"sustenance_shortfall";
        r.severity=DiagnosticSeverity::Warning;
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
  // Degraded structures: complete+enabled buildings worn at or below the
  // operational condition floor silently contribute nothing to surface
  // output (they fail `operational()` before power/staffing allocation).
  // The engine settlement projection carries the authoritative
  // per-structure condition — no rules are re-derived here.
  if(records.size()<maximum){
    for(const auto &colony:world.colonies){
      if(records.size()>=maximum)break;
      if(colony.surface_buildings.empty())continue;
      const auto settlement=project_colony_settlement(colony);
      std::size_t degraded=0;double worst=1.0;
      for(const auto *structure:settlement.structures()){
        if(!structure->complete||!structure->enabled)continue;
        if(structure->condition>minimum_operational_condition)continue;
        ++degraded;worst=std::min(worst,structure->condition);
      }
      if(!degraded)continue;
      DiagnosticRecord r;r.tick=tick;r.game_date=format_campaign_date(day);r.subsystem="colony";
      r.event_type="degraded_structures";r.severity=DiagnosticSeverity::Warning;
      r.entity_id=colony.id;r.civilization_id=colony.civilization_id;r.system_id=colony.system_id;
      char message[160];
      std::snprintf(message,sizeof(message),
                    "%llu surface structures at or below operational condition (worst %.0f%%).",
                    static_cast<unsigned long long>(degraded),worst*100.0);
      r.message=message;
      r.values["degradedCount"]=static_cast<double>(degraded);
      r.values["worstCondition"]=worst;
      records.push_back(std::move(r));
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
  // Ordered but unreachable: a fleet with an assigned destination whose
  // route no longer assesses as reachable (fuel service lost, lane
  // broken, destination gone). Reassessed with the same authoritative
  // reach calculator orders use — flagged only when the assessment is
  // authoritative, not provisional.
  if(records.size()<maximum){
    InterstellarLaneNetwork lanes(world.systems);
    std::unordered_map<int,OperationalReachBatch> batches;
    const auto kind_for=[](FleetRole role){
      switch(role){
      case FleetRole::Military:return InterstellarMissionKind::MilitaryDeployment;
      case FleetRole::Colony:return InterstellarMissionKind::Colony;
      case FleetRole::Logistics:return InterstellarMissionKind::Logistics;
      case FleetRole::Science:return InterstellarMissionKind::ScienceSurvey;
      case FleetRole::Scout:default:return InterstellarMissionKind::ScoutReconnaissance;
      }
    };
    for(const auto &fleet:world.fleets){
      if(records.size()>=maximum)break;
      if(!fleet.is_active||!fleet.destination_system_id)continue;
      if(!world.systems.empty()&&
         std::none_of(world.systems.begin(),world.systems.end(),
                      [&](const StellarSystem &s){return s.id==*fleet.destination_system_id;})){
        DiagnosticRecord r;r.tick=tick;r.game_date=format_campaign_date(day);r.subsystem="fleet";
        r.event_type="route_unreachable";r.severity=DiagnosticSeverity::Warning;
        r.entity_id=fleet.id;r.civilization_id=fleet.civilization_id;r.system_id=fleet.current_system_id;
        r.message="Ordered destination no longer exists.";
        r.values["destinationSystemId"]=static_cast<std::int64_t>(*fleet.destination_system_id);
        records.push_back(std::move(r));continue;
      }
      auto [it,_]=batches.try_emplace(fleet.civilization_id,
          OperationalReachWorldView{world.systems,world.colonies,lanes},fleet.civilization_id);
      const auto reach=it->second.assess(fleet,*fleet.destination_system_id,kind_for(fleet.role));
      if(!reach.is_authoritative||reach.is_supported)continue;
      DiagnosticRecord r;r.tick=tick;r.game_date=format_campaign_date(day);r.subsystem="fleet";
      r.event_type="route_unreachable";r.severity=DiagnosticSeverity::Warning;
      r.entity_id=fleet.id;r.civilization_id=fleet.civilization_id;r.system_id=fleet.current_system_id;
      r.message=reach.reason;
      r.values["destinationSystemId"]=static_cast<std::int64_t>(*fleet.destination_system_id);
      r.values["routeDistanceLightYears"]=reach.route_distance_light_years;
      records.push_back(std::move(r));
    }
  }
  // Logistics supply: colonies the authoritative economy logistics
  // model rates Strained or Critical (import requirements outrunning
  // local support), and civilizations whose external systems import
  // support no represented freight corridor carries. Conditions and
  // coverage come from `economy_logistics`/`civilization_logistics_coverage`
  // — the same authoritative computations the logistics workspace and
  // voice bridge consume — not re-derived here.
  if(records.size()<maximum){
    const auto econ_construction=economic_construction_projection(world.construction);
    const auto econ_fleets=economic_fleet_projection(world.fleets);
    const EconomyWorldView econ{world.civilizations,world.bodies,econ_construction,econ_fleets};
    const SettlementBodyIndex population_index(world.colonies,world.bodies);
    for(const auto &civ:world.civilizations){
      if(records.size()>=maximum)break;
      // The authoritative queries throw when a civ lacks economy or
      // construction rows — missing rows are an invariant finding, not
      // a logistics one; skip rather than fail the whole pass.
      const auto has_economy=std::any_of(world.economies.begin(),world.economies.end(),
          [&](const auto &e){return e.civilization_id==civ.id;});
      const auto has_construction=std::any_of(econ_construction.begin(),econ_construction.end(),
          [&](const auto &s){return s.civilization_id==civ.id;});
      if(!has_economy||!has_construction)continue;
      const auto snapshot=economy_logistics(econ,world.colonies,world.economies,civ.id);
      for(const auto &colony:snapshot.colonies){
        if(records.size()>=maximum)break;
        if(colony.condition==SupplyCondition::Healthy)continue;
        DiagnosticRecord r;r.tick=tick;r.game_date=format_campaign_date(day);r.subsystem="logistics";
        r.event_type=colony.condition==SupplyCondition::Critical?"logistics_critical":"logistics_strained";
        // Severity stays Warning like every operational finding —
        // Critical is reserved for invariant violations; the
        // event_type already distinguishes the supply condition.
        r.severity=DiagnosticSeverity::Warning;
        r.entity_id=colony.colony_id;r.civilization_id=civ.id;r.system_id=colony.system_id;
        char message[160];
        std::snprintf(message,sizeof(message),
                      "Colony logistics %s: imports %.2f/day required, coverage %.0f%%.",
                      colony.condition==SupplyCondition::Critical?"critical":"strained",
                      colony.imported_support_required_per_day,colony.coverage_ratio*100.0);
        r.message=message;
        r.values["importRequiredPerDay"]=colony.imported_support_required_per_day;
        r.values["coverageRatio"]=colony.coverage_ratio;
        records.push_back(std::move(r));
      }
      // Treasury: Arrears/Depleted are the severe states the economy
      // workspace and voice bridge already classify via the same
      // authoritative assess_treasury; Surplus/Deficit are normal.
      if(records.size()<maximum){
        const auto eit=std::find_if(world.economies.begin(),world.economies.end(),
            [&](const auto &e){return e.civilization_id==civ.id;});
        // assess_treasury rejects non-finite/negative inputs — corrupt
        // values are invariant findings, not a reason to fail the pass.
        if(eit!=world.economies.end()&&std::isfinite(eit->credits)&&eit->credits>=0.0&&
           std::isfinite(eit->operating_arrears)&&eit->operating_arrears>=0.0){
          const auto flow=economy_credit_flow(econ,world.colonies,world.economies,civ.id);
          const auto health=assess_treasury(eit->credits,flow.net_credits_per_day,eit->operating_arrears);
          if(health.state==TreasuryHealthState::Arrears||health.state==TreasuryHealthState::Depleted){
            DiagnosticRecord r;r.tick=tick;r.game_date=format_campaign_date(day);r.subsystem="economy";
            r.event_type=health.state==TreasuryHealthState::Arrears?"treasury_arrears":"treasury_depleted";
            r.severity=DiagnosticSeverity::Warning;
            r.civilization_id=civ.id;
            char message[192];
            if(health.state==TreasuryHealthState::Arrears)
              std::snprintf(message,sizeof(message),
                            "Treasury carries %.2f credits of unpaid operating arrears (net %.2f/day).",
                            eit->operating_arrears,flow.net_credits_per_day);
            else
              std::snprintf(message,sizeof(message),
                            "Treasury depleted with a %.2f/day deficit.",
                            flow.net_credits_per_day);
            r.message=message;
            r.values["balance"]=eit->credits;
            r.values["netCreditsPerDay"]=flow.net_credits_per_day;
            r.values["operatingArrears"]=eit->operating_arrears;
            records.push_back(std::move(r));
          }
        }
      }
      // Corridor coverage is only meaningful for civs with colonies
      // outside their home system — skip the heavier computation for
      // homebound civilizations.
      const auto has_external=std::any_of(world.colonies.begin(),world.colonies.end(),
          [&](const auto &c){return c.civilization_id==civ.id&&c.system_id!=civ.home_system_id;});
      if(records.size()<maximum&&has_external){
        const auto coverage=civilization_logistics_coverage(econ,world.colonies,world.economies,civ.id);
        if(coverage.has_unrepresented_interstellar_support_gap){
          DiagnosticRecord r;r.tick=tick;r.game_date=format_campaign_date(day);r.subsystem="logistics";
          r.event_type="freight_corridor_gap";r.severity=DiagnosticSeverity::Warning;
          r.civilization_id=civ.id;
          char message[192];
          std::snprintf(message,sizeof(message),
                        "External colonies import %.2f support/day with no represented freight corridor.",
                        coverage.unrepresented_interstellar_support_per_day);
          r.message=message;
          r.values["unrepresentedSupportPerDay"]=coverage.unrepresented_interstellar_support_per_day;
          r.values["externalSystemCount"]=static_cast<double>(coverage.external_system_count);
          records.push_back(std::move(r));
        }
      }
      // Corridor saturation: the projected freight network reports
      // per-link utilization — a link holding its full committed
      // tonnage is the binding constraint on home-system support, a
      // signal the colony-level snapshots cannot express. The network
      // is projected from the same authoritative home_system_logistics
      // the workspace consumes; nothing is re-derived here.
      if(records.size()<maximum){
        const auto home=home_system_logistics(econ,world.colonies,world.economies,civ.id);
        const auto projected=project_home_logistics_network(home);
        for(const auto& [route_id,utilization]:projected.route_utilization()){
          if(records.size()>=maximum)break;
          if(utilization<0.999)continue;
          const auto* route=projected.route(route_id);
          if(!route)continue;
          DiagnosticRecord r;r.tick=tick;r.game_date=format_campaign_date(day);r.subsystem="logistics";
          r.event_type="logistics_link_saturated";r.severity=DiagnosticSeverity::Warning;
          r.entity_id=static_cast<int>(route_id);r.civilization_id=civ.id;r.system_id=civ.home_system_id;
          char message[192];
          std::snprintf(message,sizeof(message),
                        "Freight corridor %llu->%llu saturated: %.0f%% of committed capacity in flight.",
                        static_cast<unsigned long long>(route->path.front()),
                        static_cast<unsigned long long>(route->path.back()),
                        utilization*100.0);
          r.message=message;
          r.values["linkId"]=static_cast<double>(route_id);
          r.values["fromNode"]=static_cast<double>(route->path.front());
          r.values["toNode"]=static_cast<double>(route->path.back());
          r.values["utilization"]=utilization;
          records.push_back(std::move(r));
        }
      }
      // Population unrest: the cohort projection's migration_pressure
      // query reads the colony's emigration pressure from the
      // authoritative wellbeing inputs (stability, employment,
      // crowding, sustenance ratios) — const query, no growth is
      // simulated. A colony whose cohort wants to leave at >=10%/year
      // is a loyalty risk no existing finding covers.
      if(records.size()<maximum){
        const auto cit=std::find_if(econ_construction.begin(),econ_construction.end(),
            [&](const auto &s){return s.civilization_id==civ.id;});
        const bool automation=cit!=econ_construction.end()&&
            std::find(cit->completed_project_ids.begin(),cit->completed_project_ids.end(),
                      "industrial_automation")!=cit->completed_project_ids.end();
        for(const auto &colony:world.colonies){
          if(records.size()>=maximum)break;
          if(colony.civilization_id!=civ.id||colony.kind!=SettlementKind::Colony)continue;
          const auto projected=project_colony_population(
              colony,population_index.bodies_for(colony),30.0,automation);
          const double pressure=projected.population.migration_pressure(
              projected.cohort,projected.conditions);
          if(pressure<0.10)continue;
          DiagnosticRecord r;r.tick=tick;r.game_date=format_campaign_date(day);r.subsystem="colony";
          r.event_type="population_unrest";r.severity=DiagnosticSeverity::Warning;
          r.entity_id=colony.id;r.civilization_id=civ.id;r.system_id=colony.system_id;
          char message[192];
          std::snprintf(message,sizeof(message),
                        "Colony population unrest: %.0f%%/year emigration pressure.",
                        pressure*100.0);
          r.message=message;
          r.values["emigrationPressurePerYear"]=pressure;
          // The projection always carries exactly one cohort.
          r.values["employmentRate"]=projected.population.cohorts().front()->employment_rate;
          r.values["foodRatio"]=projected.conditions.food_ratio;
          r.values["overcrowding"]=projected.conditions.overcrowding;
          records.push_back(std::move(r));
        }
      }
      // Advisor spotlight: the engine decision machinery picks the civ's
      // single highest-utility operational finding emitted this pass —
      // the triage pointer a developer reads first. A fresh mind per
      // pass keeps the evaluation stateless (hysteresis/cooldowns are
      // temporal and do not apply to a one-shot ranking); the journal
      // still records utility and candidate count. The commit emits the
      // spotlight record — a real effect, not a shadow evaluation.
      if(records.size()<maximum){
        static const std::unordered_map<std::string,double> severity_weights{
            {"logistics_critical",.95},{"treasury_depleted",.95},
            {"treasury_arrears",.9},{"logistics_link_saturated",.85},
            {"population_unrest",.8},{"freight_corridor_gap",.75},
            {"logistics_strained",.7},{"sustenance_shortfall",.65},
            {"power_shortfall",.6},{"degraded_structures",.4}};
        StrategicMind mind(8);
        for(const auto &finding:records){
          if(finding.civilization_id!=civ.id||finding.subsystem=="advisor")continue;
          const auto weight=severity_weights.find(finding.event_type);
          if(weight==severity_weights.end())continue;
          const std::string action_id=finding.subsystem+"."+finding.event_type+
              "."+std::to_string(finding.entity_id.value_or(-1));
          mind.add_action({action_id,"spotlight",
              [utility=weight->second]{return utility;},
              [&,finding=finding,utility=weight->second]{
                if(records.size()>=maximum)return;
                DiagnosticRecord r;r.tick=tick;r.game_date=format_campaign_date(day);
                r.subsystem="advisor";r.event_type="advisor_spotlight";
                r.severity=DiagnosticSeverity::Warning;
                r.entity_id=finding.entity_id;r.civilization_id=civ.id;
                r.system_id=finding.system_id;
                r.message="Top priority: "+finding.message;
                r.values["utility"]=utility;
                r.values["sourceEventType"]=finding.event_type;
                records.push_back(std::move(r));
              }});
        }
        (void)mind.decide("spotlight",day,0.0);
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
    positive(c.stability,"Stability",c.id,"colony");
    positive(c.stored_food_population_days_millions,"Food reserve",c.id,"colony");
    positive(c.stored_water_population_days_millions,"Water reserve",c.id,"colony");
    positive(c.stored_extracted_materials,"Extracted material reserve",c.id,"colony");
    (void)ids(c.surface_buildings,&SurfaceBuilding::id,"construction");
    for(const auto &b:c.surface_buildings){positive(b.industry_progress,"Building progress",b.id,"construction");
      positive(b.condition,"Building condition",b.id,"construction");
      positive(b.stored_power_days,"Building power reserve",b.id,"construction");
      if(!std::isfinite(b.x)||!std::isfinite(b.z))emit("construction","invalid_position",b.id,"Surface building position is not finite.");}
  }
  for(const auto &e:w.economies){
    if(!civilizations.contains(e.civilization_id))emit("economy","orphaned_economy",e.civilization_id,"Economy references an absent civilization.");
    positive(e.credits,"Credits",e.civilization_id,"economy");positive(e.industry,"Industry",e.civilization_id,"economy");positive(e.science,"Science",e.civilization_id,"economy");
    positive(e.operating_arrears,"Operating arrears",e.civilization_id,"economy");
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
