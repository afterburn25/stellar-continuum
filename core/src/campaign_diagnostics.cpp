#include <stellar/core/campaign_diagnostics.hpp>
#include <stellar/core/campaign_calendar.hpp>
#include <stellar/core/campaign_colony_projection.hpp>
#include <stellar/core/colony_biology.hpp>
#include <stellar/core/campaign_economy.hpp>
#include <stellar/core/campaign_economy_projection.hpp>
#include <stellar/core/campaign_logistics_projection.hpp>
#include <stellar/core/campaign_population_projection.hpp>
#include <stellar/core/campaign_warfare_projection.hpp>
#include <stellar/core/construction_state.hpp>
#include <stellar/core/fleet_combat_intelligence.hpp>
#include <stellar/core/fleet_reach.hpp>
#include <stellar/core/fleet_state.hpp>
#include <stellar/core/lane_network.hpp>
#include <stellar/core/legacy_technology.hpp>
#include <stellar/core/logistics.hpp>
#include <stellar/core/settlement_body_index.hpp>
#include <stellar/core/surface_economy.hpp>
#include <stellar/engine/strategic_ai.hpp>
#include <cmath>
#include <cstdio>
#include <exception>
#include <optional>
#include <unordered_map>
#include <unordered_set>

namespace stellar::core {
std::vector<stellar::engine::DiagnosticRecord> inspect_campaign_operations(
    const FreshCampaignState &world,std::uint64_t tick,double day,std::size_t maximum){
  using namespace stellar::engine;
  if(maximum<1||maximum>4096)throw std::invalid_argument("Invalid operational finding bound.");
  std::vector<DiagnosticRecord> records;
  // Honest truncation: any candidate loop that stops at the bound, or
  // any block skipped because it is already full, means findings may
  // have been dropped — the pass appends a findings_truncated marker
  // so callers can distinguish "128 findings" from "128 of N".
  bool truncated=false;
  for(const auto &fleet:world.fleets){
    if(!fleet.is_active||!fleet.return_to_base_failure_reason||fleet.return_to_base_failure_reason->empty())continue;
    DiagnosticRecord r;r.tick=tick;r.game_date=format_campaign_date(day);r.subsystem="fleet";
    r.event_type="return_route_unavailable";r.severity=DiagnosticSeverity::Warning;
    r.entity_id=fleet.id;r.civilization_id=fleet.civilization_id;r.system_id=fleet.current_system_id;
    r.message=*fleet.return_to_base_failure_reason;
    r.values["fuelLightYears"]=fleet.fuel_remaining_light_years;
    records.push_back(std::move(r));if(records.size()==maximum){truncated=true;break;}
  }
  // Sustenance shortfalls: colonies whose food/water demand outruns
  // installed supply over a 30-day horizon. The engine economy-analysis
  // framework does the demand/bottleneck math; the projection adapter
  // reshapes authoritative sustenance state (same functions the economy
  // phase calls) — no rules are duplicated or re-derived here.
  if(records.size()>=maximum)truncated=true;else{
    static const auto catalog=sustenance_economy_catalog();
    const SettlementBodyIndex sustenance_index(world.colonies,world.bodies);
    for(const auto &colony:world.colonies){
      if(records.size()>=maximum){truncated=true;break;}
      if(colony.kind!=SettlementKind::Colony)continue;
      // The authoritative queries refuse corrupt colonies (uncatalogued
      // building types, unresolved bodies, negative ids) — invariants
      // report them; skip rather than fail the whole pass.
      std::vector<engine::EconomyDiagnostic> demands;
      try{demands=analyze_colony_sustenance(colony,sustenance_index.bodies_for(colony),30.0,catalog);}
      catch(const std::exception&){continue;}
      for(const auto &d:demands){
        if(!d.bottleneck)continue;
        if(records.size()>=maximum){truncated=true;break;}
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
  if(records.size()>=maximum)truncated=true;else{
    for(const auto &colony:world.colonies){
      if(records.size()>=maximum){truncated=true;break;}
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
  if(records.size()>=maximum)truncated=true;else{
    std::unordered_map<int,std::unordered_set<int>> system_owners;
    for(const auto &colony:world.colonies){
      if(colony.kind!=SettlementKind::Colony)continue;
      system_owners[colony.system_id].insert(colony.civilization_id);
    }
    if(!system_owners.empty()){
      // The projection refuses fleets the engine model cannot
      // represent (non-positive/NaN strategic speed) — invariants flag
      // them; skip the block rather than fail the whole pass.
      std::optional<engine::WarfareModel> theater;
      try{theater.emplace(project_warfare_theater(world.fleets,world.systems));}
      catch(const std::exception&){}
      if(theater)for(const auto &fleet:world.fleets){
        if(records.size()>=maximum){truncated=true;break;}
        if(!fleet.is_active||!fleet.current_system_id||
           fleet.transit_phase!=FleetTransitPhase::None)continue;
        const auto owners=system_owners.find(*fleet.current_system_id);
        if(owners==system_owners.end()||owners->second.contains(fleet.civilization_id))continue;
        const auto report=theater->report(static_cast<std::uint64_t>(fleet.id));
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
  if(records.size()>=maximum)truncated=true;else{
    // Lane construction refuses duplicate system ids and reach
    // evaluation refuses non-finite metrics — invariants flag both;
    // skip rather than fail the whole pass.
    std::optional<InterstellarLaneNetwork> lanes;
    try{lanes.emplace(world.systems);}catch(const std::exception&){}
    if(lanes){
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
      if(records.size()>=maximum){truncated=true;break;}
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
      MissionReachAssessment reach;
      try{
        auto [it,_]=batches.try_emplace(fleet.civilization_id,
            OperationalReachWorldView{world.systems,world.colonies,*lanes},fleet.civilization_id);
        reach=it->second.assess(fleet,*fleet.destination_system_id,kind_for(fleet.role));
      }catch(const std::exception&){continue;}
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
  }
  // Logistics supply: colonies the authoritative economy logistics
  // model rates Strained or Critical (import requirements outrunning
  // local support), and civilizations whose external systems import
  // support no represented freight corridor carries. Conditions and
  // coverage come from `economy_logistics`/`civilization_logistics_coverage`
  // — the same authoritative computations the logistics workspace and
  // voice bridge consume — not re-derived here.
  if(records.size()>=maximum)truncated=true;else{
    const auto econ_construction=economic_construction_projection(world.construction);
    const auto econ_fleets=economic_fleet_projection(world.fleets);
    const EconomyWorldView econ{world.civilizations,world.bodies,econ_construction,econ_fleets};
    const SettlementBodyIndex population_index(world.colonies,world.bodies);
    for(const auto &civ:world.civilizations){
      if(records.size()>=maximum){truncated=true;break;}
      // The authoritative queries throw when a civ lacks economy or
      // construction rows — missing rows are an invariant finding, not
      // a logistics one; skip rather than fail the whole pass.
      const auto has_economy=std::any_of(world.economies.begin(),world.economies.end(),
          [&](const auto &e){return e.civilization_id==civ.id;});
      const auto has_construction=std::any_of(econ_construction.begin(),econ_construction.end(),
          [&](const auto &s){return s.civilization_id==civ.id;});
      if(!has_economy||!has_construction)continue;
      // Snapshot assembly also refuses duplicate colony ids — an
      // invariant finding; skip the civ rather than fail the pass.
      CivilizationLogisticsSnapshot snapshot;
      try{snapshot=economy_logistics(econ,world.colonies,world.economies,civ.id);}
      catch(const std::exception&){continue;}
      for(const auto &colony:snapshot.colonies){
        if(records.size()>=maximum){truncated=true;break;}
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
      if(records.size()>=maximum)truncated=true;else{
        const auto eit=std::find_if(world.economies.begin(),world.economies.end(),
            [&](const auto &e){return e.civilization_id==civ.id;});
        // assess_treasury rejects non-finite/negative inputs — corrupt
        // values are invariant findings, not a reason to fail the pass.
        if(eit!=world.economies.end()&&std::isfinite(eit->credits)&&eit->credits>=0.0&&
           std::isfinite(eit->operating_arrears)&&eit->operating_arrears>=0.0){
          // economy_credit_flow evaluates colony habitat support, which
          // refuses corrupt colonies (uncatalogued species, unresolved
          // bodies) — invariants flag them; skip rather than fail.
          try{
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
          }catch(const std::exception&){}
        }
      }
      // Corridor coverage is only meaningful for civs with colonies
      // outside their home system — skip the heavier computation for
      // homebound civilizations.
      const auto has_external=std::any_of(world.colonies.begin(),world.colonies.end(),
          [&](const auto &c){return c.civilization_id==civ.id&&c.system_id!=civ.home_system_id;});
      if(records.size()>=maximum)truncated=true;else if(has_external){
        CivilizationLogisticsCoverage coverage;
        try{coverage=civilization_logistics_coverage(econ,world.colonies,world.economies,civ.id);}
        catch(const std::exception&){coverage={};}
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
      if(records.size()>=maximum)truncated=true;else{
        HomeSystemLogisticsNetwork home;
        try{home=home_system_logistics(econ,world.colonies,world.economies,civ.id);}
        catch(const std::exception&){home={};}
        const auto projected=project_home_logistics_network(home);
        for(const auto& [route_id,utilization]:projected.route_utilization()){
          if(records.size()>=maximum){truncated=true;break;}
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
      if(records.size()>=maximum)truncated=true;else{
        const auto cit=std::find_if(econ_construction.begin(),econ_construction.end(),
            [&](const auto &s){return s.civilization_id==civ.id;});
        const bool automation=cit!=econ_construction.end()&&
            std::find(cit->completed_project_ids.begin(),cit->completed_project_ids.end(),
                      "industrial_automation")!=cit->completed_project_ids.end();
        for(const auto &colony:world.colonies){
          if(records.size()>=maximum){truncated=true;break;}
          if(colony.civilization_id!=civ.id||colony.kind!=SettlementKind::Colony)continue;
          // The projection refuses corrupt colonies (uncatalogued
          // species/building types, unresolved bodies, negative ids) —
          // invariants report them; skip rather than fail the pass.
          ColonyPopulationProjection projected;
          try{projected=project_colony_population(
              colony,population_index.bodies_for(colony),30.0,automation);}
          catch(const std::exception&){continue;}
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
      if(records.size()>=maximum)truncated=true;else{
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
                if(records.size()>=maximum){truncated=true;return;}
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
  if(truncated){
    DiagnosticRecord r;r.tick=tick;r.game_date=format_campaign_date(day);
    r.subsystem="diagnostics";r.event_type="findings_truncated";
    r.severity=DiagnosticSeverity::Warning;
    r.message="Operational finding bound reached; additional findings may have been dropped.";
    r.values["maximumFindings"]=static_cast<std::int64_t>(maximum);
    records.push_back(std::move(r));
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
  // Dropped findings are counted and surfaced as a findings_truncated
  // marker so a capped pass cannot hide invariant corruption silently.
  std::size_t dropped=0;
  const auto emit=[&](std::string subsystem,std::string type,int id,std::string message){
    if(findings.size()>=maximum){++dropped;return;}
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
  const auto fleet_ids=ids(w.fleets,&FleetState::id,"fleet");
  const auto colony_ids=ids(w.colonies,&Colony::id,"colony");
  (void)ids(w.economies,&CivilizationEconomy::civilization_id,"economy");
  (void)ids(w.construction,&ConstructionState::civilization_id,"construction");
  (void)ids(w.technologies,&TechnologyState::civilization_id,"research");
  (void)ids(w.shipyards,&ShipyardState::civilization_id,"shipyard");
  const auto positive=[&](double value,std::string_view name,int id,std::string_view subsystem){
    if(!std::isfinite(value)||value< -1e-6)emit(std::string(subsystem),"invalid_nonnegative_value",id,std::string(name)+" is non-finite or negative.");
  };
  // Semantic bounds: the authoritative writers clamp these (refuel
  // caps at capacity*service, freight loads cap at capacity, the
  // fleet loader enforces transit progress in [0,1], funding
  // fractions clamp to [0,1]) — an in-memory violation means the
  // state was corrupted after load. Skipped when either side is
  // non-finite or the bound is negative: `positive` already names it.
  const auto bounded=[&](double value,double bound,std::string_view name,int id,std::string_view subsystem){
    if(std::isfinite(value)&&std::isfinite(bound)&&bound>=0.0&&value>bound+1e-6)
      emit(std::string(subsystem),"out_of_range",id,std::string(name)+" exceeds its authoritative bound.");
  };
  // Reference lookups the operations pass relies on: bodies keyed by
  // (system,id) — a body that exists in another system is still
  // unresolvable for the colony — plus catalogued building types and
  // species so the checks below flag what the ops pass must skip.
  std::unordered_set<std::uint64_t> body_keys;body_keys.reserve(w.bodies.size());
  for(const auto &b:w.bodies)
    body_keys.insert((std::uint64_t{static_cast<std::uint32_t>(b.system_id)}<<32)|
                     static_cast<std::uint32_t>(b.id));
  std::unordered_set<std::string_view> known_types,known_species,known_techs;
  for(const auto &d:surface_building_catalog())known_types.insert(d.id);
  for(const auto &p:species_biology_profiles())known_species.insert(p.id);
  for(const auto &t:legacy_technology_catalog())known_techs.insert(t.id);
  for(const auto &s:w.systems){
    if(!std::isfinite(s.position.x)||!std::isfinite(s.position.y)||
        (s.position.depth_light_years&&!std::isfinite(*s.position.depth_light_years)))
      emit("galaxy","invalid_position",s.id,"System position is not finite.");
    if(s.engulfed_planets<0)emit("galaxy","invalid_nonnegative_value",s.id,"Engulfed planet count is negative.");
  }
  if(w.stellar_activity_day)positive(*w.stellar_activity_day,"Stellar activity day",0,"galaxy");
  if(w.core){
    if(!std::isfinite(w.core->position.x)||!std::isfinite(w.core->position.y))
      emit("galaxy","invalid_position",0,"Galactic core position is not finite.");
    positive(w.core->exclusion_radius,"Core exclusion radius",0,"galaxy");
  }
  for(const auto &b:w.bodies){
    if(!systems.contains(b.system_id))emit("planet","orphaned_body",b.id,"Planetary body references an absent system.");
    if(b.parent_body_id&&!bodies.contains(*b.parent_body_id))emit("planet","orphaned_parent",b.id,"Moon references an absent parent body.");
    // Parent chains that revisit a body can never resolve to a host.
    if(b.parent_body_id){
      std::unordered_set<int> seen{b.id};std::optional<int> cur=b.parent_body_id;
      while(cur&&seen.insert(*cur).second){
        const auto it=std::find_if(w.bodies.begin(),w.bodies.end(),[&](const auto &x){return x.id==*cur;});
        cur=it==w.bodies.end()?std::nullopt:it->parent_body_id;
      }
      if(cur)emit("planet","cyclic_parent",b.id,"Body parent chain revisits a body.");
    }
    // Mirrors validate_planetary_body's authoritative bounds — the
    // loader enforces these, so violations are post-load corruption.
    if(!std::isfinite(b.radius_earth)||b.radius_earth<=0.0)
      emit("planet","invalid_positive_value",b.id,"Body radius is non-finite or non-positive.");
    if(!std::isfinite(b.mass_earth)||b.mass_earth<=0.0)
      emit("planet","invalid_positive_value",b.id,"Body mass is non-finite or non-positive.");
    if(b.orbit_index<0)emit("planet","invalid_nonnegative_value",b.id,"Orbit index is negative.");
    if(!std::isfinite(b.orbital_eccentricity)||b.orbital_eccentricity<0.0||b.orbital_eccentricity>=1.0)
      emit("planet","out_of_range",b.id,"Orbital eccentricity is outside [0, 1).");
    if(!std::isfinite(b.orbital_inclination_degrees)||b.orbital_inclination_degrees<0.0||b.orbital_inclination_degrees>180.0)
      emit("planet","out_of_range",b.id,"Orbital inclination is outside [0, 180] degrees.");
    if(b.kind==PlanetaryBodyKind::Moon&&!b.parent_body_id)
      emit("planet","invalid_body_parent",b.id,"Moon has no parent body.");
    if(b.kind!=PlanetaryBodyKind::Moon&&b.parent_body_id)
      emit("planet","invalid_body_parent",b.id,"Primary body cannot have a parent body.");
  }
  for(const auto &c:w.colonies){
    if(!systems.contains(c.system_id)||!civilizations.contains(c.civilization_id)||
        (c.planetary_body_id&&!body_keys.contains((std::uint64_t{static_cast<std::uint32_t>(c.system_id)}<<32)|
                                                 static_cast<std::uint32_t>(*c.planetary_body_id))))
      emit("colony","orphaned_colony",c.id,"Colony has an absent owner, system or body.");
    if(c.id<0)emit("colony","invalid_nonnegative_value",c.id,"Colony ID is negative.");
    if(c.population_millions>0.0&&!known_species.contains(c.population_species_id))
      emit("colony","unknown_species",c.id,"Populated colony references an uncatalogued species.");
    positive(c.population_millions,"Population",c.id,"colony");positive(c.infrastructure,"Infrastructure",c.id,"colony");
    positive(c.stability,"Stability",c.id,"colony");
    positive(c.stored_food_population_days_millions,"Food reserve",c.id,"colony");
    positive(c.stored_water_population_days_millions,"Water reserve",c.id,"colony");
    positive(c.stored_extracted_materials,"Extracted material reserve",c.id,"colony");
    if(c.remaining_extractable_materials)positive(*c.remaining_extractable_materials,"Remaining extractable materials",c.id,"colony");
    positive(c.surface_hub_upgrade_days_remaining,"Hub upgrade remaining",c.id,"colony");
    (void)ids(c.surface_buildings,&SurfaceBuilding::id,"construction");
    for(const auto &b:c.surface_buildings){positive(b.industry_progress,"Building progress",b.id,"construction");
      positive(b.condition,"Building condition",b.id,"construction");
      positive(b.stored_power_days,"Building power reserve",b.id,"construction");
      if(!known_types.contains(b.type_id))emit("construction","unknown_building_type",b.id,"Surface building has an uncatalogued type.");
      if(!std::isfinite(b.x)||!std::isfinite(b.z))emit("construction","invalid_position",b.id,"Surface building position is not finite.");}
  }
  for(const auto &c:w.civilizations)
    if(!systems.contains(c.home_system_id))
      emit("civilization","orphaned_home",c.id,"Civilization references an absent home system.");
  for(const auto &t:w.technologies){
    if(!civilizations.contains(t.civilization_id))emit("research","orphaned_research",t.civilization_id,"Research state references an absent civilization.");
    positive(t.active_research_progress,"Research progress",t.civilization_id,"research");
    for(const auto &id:t.completed_technology_ids.values())
      if(!known_techs.contains(id))emit("research","unknown_technology",t.civilization_id,"Completed technology is not in the catalog.");
    if(t.active_research_id&&!known_techs.contains(*t.active_research_id))
      emit("research","unknown_technology",t.civilization_id,"Active research is not in the catalog.");
  }
  for(const auto &c:w.construction){
    if(!civilizations.contains(c.civilization_id))emit("construction","orphaned_construction",c.civilization_id,"Construction state references an absent civilization.");
    positive(c.active_project_progress,"Project progress",c.civilization_id,"construction");
    positive(c.active_project_authorization_credits,"Authorized credits",c.civilization_id,"construction");
    if(c.queued_projects.size()>maximum_queued_construction_projects)
      emit("construction","queue_overflow",c.civilization_id,"Construction queue exceeds the canonical bound.");
  }
  for(const auto &y:w.shipyards){
    if(!civilizations.contains(y.civilization_id))emit("shipyard","orphaned_shipyard",y.civilization_id,"Shipyard state references an absent civilization.");
    positive(y.active_build_progress,"Build progress",y.civilization_id,"shipyard");
    positive(y.active_authorization_credits,"Authorized credits",y.civilization_id,"shipyard");
    positive(y.reserved_population_millions,"Reserved population",y.civilization_id,"shipyard");
    if(y.reserved_population_species_id&&!known_species.contains(*y.reserved_population_species_id))
      emit("shipyard","unknown_species",y.civilization_id,"Shipyard reserves an uncatalogued species.");
    if(y.reserved_population_source_colony_id&&!colony_ids.contains(*y.reserved_population_source_colony_id))
      emit("shipyard","orphaned_colony",y.civilization_id,"Shipyard reserves population from an absent colony.");
  }
  for(const auto &e:w.economies){
    if(!civilizations.contains(e.civilization_id))emit("economy","orphaned_economy",e.civilization_id,"Economy references an absent civilization.");
    positive(e.credits,"Credits",e.civilization_id,"economy");positive(e.industry,"Industry",e.civilization_id,"economy");positive(e.science,"Science",e.civilization_id,"economy");
    positive(e.operating_arrears,"Operating arrears",e.civilization_id,"economy");
    bounded(e.last_research_funding_fraction,1.0,"Research funding fraction",e.civilization_id,"economy");
    bounded(e.last_base_operations_funding_fraction,1.0,"Operations funding fraction",e.civilization_id,"economy");
  }
  for(const auto &f:w.fleets){
    if(!civilizations.contains(f.civilization_id))emit("fleet","orphaned_fleet",f.id,"Fleet references an absent civilization.");
    for(const auto system:{f.current_system_id,f.destination_system_id,f.transit_origin_system_id,f.transit_target_system_id})
      if(system&&!systems.contains(*system))emit("fleet","orphaned_location",f.id,"Fleet references an absent system.");
    if(!std::isfinite(f.position.x)||!std::isfinite(f.position.y))emit("fleet","invalid_position",f.id,"Fleet position is not finite.");
    if(!std::isfinite(f.local_transit_start.x)||!std::isfinite(f.local_transit_start.y)||
        !std::isfinite(f.local_transit_position.x)||!std::isfinite(f.local_transit_position.y)||
        !std::isfinite(f.local_transit_target.x)||!std::isfinite(f.local_transit_target.y))
      emit("fleet","invalid_position",f.id,"Local transit vectors are not finite.");
    positive(f.sensor_range,"Sensor range",f.id,"fleet");
    if(f.mission_order_revision<0)emit("fleet","invalid_nonnegative_value",f.id,"Mission order revision is negative.");
    for(const auto hop:f.planned_route_system_ids)
      if(!systems.contains(hop)){emit("fleet","orphaned_route_hop",f.id,"Planned route references an absent system.");break;}
    if(f.stellar_transit_path.size()>132)
      emit("fleet","route_overflow",f.id,"Stellar transit path exceeds the persisted route limit.");
    positive(f.fuel_remaining_light_years,"Fuel",f.id,"fleet");positive(f.embarked_population_millions,"Embarked population",f.id,"fleet");
    // Strictly positive: the warfare projection refuses speed <= 0.
    if(!std::isfinite(f.strategic_speed)||f.strategic_speed<=0.0)
      emit("fleet","invalid_positive_value",f.id,"Strategic speed is non-finite or non-positive.");
    if(f.is_active&&!f.current_system_id&&f.transit_phase!=FleetTransitPhase::InterstellarWarp)
      emit("fleet","missing_location",f.id,"Active non-warping fleet has no system location.");
    // Order and cargo state: dangling refs and corrupt magnitudes the
    // simulation dereferences every tick.
    positive(f.transit_progress,"Transit progress",f.id,"fleet");
    bounded(f.transit_progress,1.0,"Transit progress",f.id,"fleet");
    positive(f.maximum_leg_range_light_years,"Maximum leg range",f.id,"fleet");
    positive(f.fuel_capacity_light_years,"Fuel capacity",f.id,"fleet");
    bounded(f.fuel_remaining_light_years,f.fuel_capacity_light_years,"Fuel",f.id,"fleet");
    positive(f.cargo_material_capacity,"Cargo capacity",f.id,"fleet");
    positive(f.cargo_materials,"Cargo load",f.id,"fleet");
    bounded(f.cargo_materials,f.cargo_material_capacity,"Cargo load",f.id,"fleet");
    positive(f.settlement_days_completed,"Settlement progress",f.id,"fleet");
    positive(f.reconnaissance_days_completed,"Reconnaissance progress",f.id,"fleet");
    if(f.destination_planetary_body_id&&!bodies.contains(*f.destination_planetary_body_id))
      emit("fleet","orphaned_destination_body",f.id,"Fleet destination references an absent body.");
    if(f.settlement_body_id&&!bodies.contains(*f.settlement_body_id))
      emit("fleet","orphaned_settlement_body",f.id,"Settlement order references an absent body.");
    if(f.reconnaissance_system_id&&!systems.contains(*f.reconnaissance_system_id))
      emit("fleet","orphaned_reconnaissance",f.id,"Reconnaissance order references an absent system.");
    if(f.freight_target_outpost_id&&!colony_ids.contains(*f.freight_target_outpost_id))
      emit("fleet","orphaned_freight",f.id,"Freight order references an absent outpost.");
    if(f.freight_home_colony_id&&!colony_ids.contains(*f.freight_home_colony_id))
      emit("fleet","orphaned_freight",f.id,"Freight order references an absent home colony.");
    if(f.embarked_population_species_id&&!known_species.contains(*f.embarked_population_species_id))
      emit("fleet","unknown_species",f.id,"Fleet embarks an uncatalogued species.");
    if(f.combat){
      positive(f.combat->shields,"Shields",f.id,"fleet");
      positive(f.combat->armor,"Armor",f.id,"fleet");
      positive(f.combat->hull,"Hull",f.id,"fleet");
      positive(f.combat->weapon_cooldown_remaining_days,"Weapon cooldown",f.id,"fleet");
      positive(f.combat->retreat_progress_days,"Retreat progress",f.id,"fleet");
      if(f.combat->target_fleet_id&&!fleet_ids.contains(*f.combat->target_fleet_id))
        emit("fleet","orphaned_target",f.id,"Attack order references an absent fleet.");
      if(f.combat->defend_system_id&&!systems.contains(*f.combat->defend_system_id))
        emit("fleet","orphaned_defense",f.id,"Defense order references an absent system.");
      if(f.combat->disengaged_system_id&&!systems.contains(*f.combat->disengaged_system_id))
        emit("fleet","orphaned_disengagement",f.id,"Disengagement references an absent system.");
    }
  }
  // Knowledge state: observers must be civilizations and known
  // ids must resolve — observers dereference these per tick.
  {
    const auto knowledge=w.knowledge.snapshot();
    for(const auto &entry:knowledge.systems){
      if(!civilizations.contains(entry.observer_id))
        emit("knowledge","orphaned_observer",entry.observer_id,"Knowledge observer is an absent civilization.");
      for(const auto id:entry.values)
        if(!systems.contains(id)){emit("knowledge","orphaned_known_system",entry.observer_id,"Knowledge references an absent system.");break;}
    }
    for(const auto &entry:knowledge.civilizations){
      if(!civilizations.contains(entry.observer_id))
        emit("knowledge","orphaned_observer",entry.observer_id,"Knowledge observer is an absent civilization.");
      for(const auto id:entry.values)
        if(!civilizations.contains(id)){emit("knowledge","orphaned_known_civilization",entry.observer_id,"Knowledge references an absent civilization.");break;}
    }
  }
  // Combat intelligence is persisted: observers must be
  // civilizations, observed ids must be fleets, magnitudes bounded.
  std::unordered_map<int,int> observation_counts;
  for(const auto &o:w.combat_intelligence){
    if(!civilizations.contains(o.observer_id))
      emit("combat","orphaned_observer",o.fleet_id,"Power observation observer is an absent civilization.");
    if(!fleet_ids.contains(o.fleet_id))
      emit("combat","orphaned_observed_fleet",o.fleet_id,"Power observation references an absent fleet.");
    positive(o.power,"Observed power",o.fleet_id,"combat");
    positive(o.observed_day,"Observed day",o.fleet_id,"combat");
    if(++observation_counts[o.observer_id]==maximum_fleet_power_observations_per_observer+1)
      emit("combat","observation_overflow",o.observer_id,"Power observations exceed the per-observer bound.");
  }
  // An active massive encounter is persisted mid-battle: its system
  // and every vessel binding must resolve.
  if(w.active_combat_encounter){
    const auto &e=*w.active_combat_encounter;
    if(!systems.contains(e.system_id))
      emit("combat","orphaned_encounter",e.system_id,"Active encounter references an absent system.");
    positive(e.started_day,"Encounter start day",e.system_id,"combat");
    if(e.last_observed_event_sequence<0)
      emit("combat","invalid_nonnegative_value",e.system_id,"Observed event sequence is negative.");
    for(const auto &v:e.vessels)
      if(!fleet_ids.contains(v.fleet_id))
        emit("combat","orphaned_encounter_vessel",v.fleet_id,"Encounter vessel binds an absent fleet.");
  }
  if(!civilizations.contains(w.player_civilization_id))emit("civilization","missing_player",w.player_civilization_id,"Player empire ID does not exist.");
  if(dropped>0){
    DiagnosticRecord r;r.tick=tick;r.game_date=format_campaign_date(day);
    r.subsystem="diagnostics";r.event_type="findings_truncated";
    r.severity=DiagnosticSeverity::Critical;
    r.message="Invariant finding bound reached; corrupt-state findings were dropped.";
    r.values["droppedFindings"]=static_cast<std::int64_t>(dropped);
    findings.push_back(std::move(r));
  }
  return findings;
}
}
