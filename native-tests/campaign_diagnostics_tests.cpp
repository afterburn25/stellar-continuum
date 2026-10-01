#include <stellar/core/campaign_diagnostics.hpp>
#include <stellar/core/galaxy_catalog.hpp>
#include <stellar/core/developer_campaign.hpp>
#include <stellar/core/persistable_fresh_campaign.hpp>
#include <nlohmann/json.hpp>
#include <algorithm>
#include <cmath>
#include <fstream>
#include <iostream>
#include <limits>
#include <thread>

using namespace stellar::core;
using namespace stellar::engine;
namespace fs=std::filesystem;
void check(bool ok,const char *message){if(!ok)throw std::runtime_error(message);}
template<class F>bool rejects(F f){try{f();}catch(const std::exception&){return true;}return false;}
int main(int argc,char **argv)try{
  check(argc==3,"Expected catalog and diagnostic output root.");
  auto world=seed_persistable_fresh_campaign(9142050,load_nearby_catalog(argv[1]),
      {"2050-03-21T00:00:00Z",250,3,0,"terran_baseline"});
  auto clean=inspect_campaign_invariants(world,0,0);
  for(const auto &r:clean)std::cerr<<r.subsystem<<" "<<r.event_type<<" "<<r.entity_id.value_or(-1)<<" "<<r.message<<'\n';
  check(clean.empty(),"Fresh canonical world failed invariants.");
  auto operations=inspect_campaign_operations(world,0,0);
  for(const auto &r:operations)std::cerr<<r.subsystem<<" "<<r.event_type<<" "<<r.entity_id.value_or(-1)<<" "<<r.message<<'\n';
  check(std::none_of(operations.begin(),operations.end(),[](const auto &r){return r.event_type=="return_route_unavailable";}),"Clean world invented an AI stall.");
  // Seeded colonies may legitimately report sustenance shortfalls (an
  // under-provisioned settlement is an operational fact, not a stall). Any
  // such finding must still name a real colony.
  for(const auto &r:operations)if(r.event_type=="sustenance_shortfall"){
    check(r.entity_id.has_value()&&r.civilization_id.has_value(),"Sustenance finding lacks identity.");
    check(std::any_of(world.colonies.begin(),world.colonies.end(),[&](const Colony &c){return c.id==*r.entity_id;}),"Sustenance finding names a nonexistent colony.");
  }
  auto stalled=world;FleetState fleet;fleet.id=917;fleet.civilization_id=world.player_civilization_id;
  fleet.current_system_id=world.systems.front().id;fleet.fuel_remaining_light_years=8;
  fleet.return_to_base_failure_reason="No owned refuelling settlement is reachable with current fuel.";stalled.fleets.push_back(fleet);
  auto warnings=inspect_campaign_operations(stalled,24,6);
  const auto stalls=std::count_if(warnings.begin(),warnings.end(),[](const auto &r){return r.event_type=="return_route_unavailable";});
  const auto stall=std::find_if(warnings.begin(),warnings.end(),[](const auto &r){return r.event_type=="return_route_unavailable";});
  check(stalls==1&&stall!=warnings.end()&&stall->severity==DiagnosticSeverity::Warning&&stall->entity_id==917,
      "Canonical return failure was not reported separately from invariants.");
  check(stalled.fleets.back().fuel_remaining_light_years==8,"Diagnostic repaired fuel.");
  stalled.fleets.back().is_active=false;
  const auto settled=inspect_campaign_operations(stalled,24,6);
  check(std::none_of(settled.begin(),settled.end(),[](const auto &r){return r.event_type=="return_route_unavailable";}),"Inactive historical ship reported as currently stranded.");
  check(rejects([&]{(void)inspect_campaign_operations(world,0,0,0);}),"Invalid operational finding bound accepted.");
  {
    // Ordered-but-unreachable: the destination no longer assesses as
    // reachable with the fleet's actual fuel/range — surfaced through
    // the same authoritative reach calculator the order path uses.
    auto stranded=world;
    FleetState deep;deep.id=918;deep.civilization_id=world.player_civilization_id;
    deep.current_system_id=world.systems.front().id;
    deep.destination_system_id=world.systems.back().id;
    deep.role=FleetRole::Military;deep.is_active=true;
    deep.fuel_remaining_light_years=1;deep.maximum_leg_range_light_years=1;
    deep.fuel_capacity_light_years=1;
    stranded.fleets.push_back(deep);
    const auto ops=inspect_campaign_operations(stranded,0,0);
    const auto unreachable=std::find_if(ops.begin(),ops.end(),[](const auto &r){return r.event_type=="route_unreachable";});
    check(unreachable!=ops.end()&&unreachable->entity_id==918,
        "Unreachable ordered destination was not reported.");
    // A fleet whose destination simply does not exist is reported too.
    auto orphaned=world;
    FleetState ghost=deep;ghost.id=919;ghost.destination_system_id=999999;
    orphaned.fleets.push_back(ghost);
    const auto ops2=inspect_campaign_operations(orphaned,0,0);
    const auto gone=std::find_if(ops2.begin(),ops2.end(),[](const auto &r){return r.event_type=="route_unreachable";});
    check(gone!=ops2.end()&&gone->entity_id==919,
        "Nonexistent ordered destination was not reported.");
  }
  {
    // The projection-backed findings fire end-to-end: a home colony
    // pushed into import dependency saturates its freight corridor, and
    // a distressed colony reports emigration pressure.
    auto distressed=world;
    const int civ_id=world.player_civilization_id;
    const auto civ_it=std::find_if(distressed.civilizations.begin(),distressed.civilizations.end(),
        [&](const Civilization &c){return c.id==civ_id;});
    check(civ_it!=distressed.civilizations.end(),"Player civ missing.");
    const int home_id=civ_it->home_system_id;
    Colony *home_first=nullptr,*home_second=nullptr;
    for(auto &c:distressed.colonies)if(c.civilization_id==civ_id&&c.system_id==home_id){
      if(!home_first)home_first=&c;else if(!home_second)home_second=&c;}
    check(home_first,"No home colony to distress.");
    if(!home_second){
      // Relocate an owned colony into the home system so the network has a corridor.
      for(auto &c:distressed.colonies)if(c.civilization_id==civ_id&&c.system_id!=home_id){
        c.system_id=home_id;home_second=&c;break;}
      check(home_second,"Could not stage a second home colony.");
    }
    // Homeworld: large surplus so the corridor — not the supply — binds.
    home_first->population_millions=500.0;home_first->infrastructure=5.0;home_first->stability=1.0;
    home_second->kind=SettlementKind::Colony;
    home_second->population_millions=500.0;home_second->infrastructure=.1;home_second->stability=.3;
    // Starve cargo handling so the corridor binds at capacity.
    auto &econ=*std::find_if(distressed.economies.begin(),distressed.economies.end(),
        [&](const CivilizationEconomy &e){return e.civilization_id==civ_id;});
    econ.last_industry_per_second=.01;
    const auto ops=inspect_campaign_operations(distressed,7,3.5);
    check(std::any_of(ops.begin(),ops.end(),[](const auto &r){return r.event_type=="logistics_link_saturated";}),
        "Saturated corridor produced no link finding.");
    check(std::any_of(ops.begin(),ops.end(),[&](const auto &r){return r.event_type=="population_unrest"&&r.entity_id==home_second->id;}),
        "Distressed colony produced no emigration-pressure finding.");
    // The advisor spotlight commits exactly once per civ and names the
    // highest-severity finding class present (critical logistics at .95
    // outranks saturation/unrest/shortfall).
    const auto spotlights=std::count_if(ops.begin(),ops.end(),
        [&](const auto &r){return r.event_type=="advisor_spotlight"&&r.civilization_id==civ_id;});
    check(spotlights==1,"Advisor did not commit exactly one spotlight.");
    const auto spotlight=std::find_if(ops.begin(),ops.end(),
        [&](const auto &r){return r.event_type=="advisor_spotlight"&&r.civilization_id==civ_id;});
    check(spotlight!=ops.end()&&spotlight->subsystem=="advisor"&&
        std::get<std::string>(spotlight->values.at("sourceEventType"))=="logistics_critical",
        "Advisor spotlighted the wrong finding class.");
  }
  {
    // Coverage sweep for the projection-backed finding classes the
    // scenarios above never trigger. Every input below is authoritative
    // state the finding consumes: settlement structure condition,
    // surface power supply/demand, the warfare theater projection, the
    // credit-flow/treasury assessment, and corridor coverage.
    auto exposed=world;
    const int civ_id=world.player_civilization_id;
    const auto civ_it=std::find_if(exposed.civilizations.begin(),exposed.civilizations.end(),
        [&](const Civilization &c){return c.id==civ_id;});
    check(civ_it!=exposed.civilizations.end(),"Player civ missing.");
    const int home_id=civ_it->home_system_id;
    int next_colony_id=1;for(const auto &c:exposed.colonies)next_colony_id=std::max(next_colony_id,c.id+1);
    // A home-system colony carrying one worn-out but commissioned lab
    // (degraded) and three healthy labs with no generator (power
    // demand 6 against the base supply of 2).
    Colony worn_site;worn_site.id=next_colony_id++;
    worn_site.civilization_id=civ_id;worn_site.system_id=home_id;
    worn_site.name="Worn site";worn_site.kind=SettlementKind::Colony;
    worn_site.population_millions=5.0;worn_site.infrastructure=.5;worn_site.stability=.8;
    worn_site.surface_hub_level=1;
    SurfaceBuilding worn;worn.id=9001;worn.type_id="science_lab";
    worn.is_complete=true;worn.is_enabled=true;worn.condition=.10;
    worn_site.surface_buildings.push_back(worn);
    for(int i=0;i<3;++i){
      SurfaceBuilding lab;lab.id=9002+i;lab.type_id="science_lab";
      lab.is_complete=true;lab.is_enabled=true;lab.condition=1.0;
      worn_site.surface_buildings.push_back(lab);
    }
    exposed.colonies.push_back(worn_site);
    // An under-provisioned colony in another owned system: import
    // requirement > 0 makes it logistics_critical and leaves the civ
    // with an unrepresented corridor gap.
    const auto external_system=std::find_if(exposed.systems.begin(),exposed.systems.end(),
        [&](const StellarSystem &s){return s.id!=home_id;});
    check(external_system!=exposed.systems.end(),"No external system to stage the corridor gap.");
    Colony frontier;frontier.id=next_colony_id++;
    frontier.civilization_id=civ_id;frontier.system_id=external_system->id;
    frontier.name="Frontier";frontier.kind=SettlementKind::Colony;
    frontier.population_millions=10.0;frontier.infrastructure=.1;frontier.stability=.8;
    exposed.colonies.push_back(frontier);
    // An armed foreign fleet parked in the home system.
    FleetState hostile;hostile.id=930;hostile.civilization_id=civ_id+4242;
    hostile.role=FleetRole::Military;hostile.is_active=true;
    hostile.current_system_id=home_id;
    exposed.fleets.push_back(hostile);
    // The theater projection refuses fleets it cannot represent — make
    // sure every staged fleet carries a positive strategic speed.
    for(auto &f:exposed.fleets)if(!(f.strategic_speed>0.0)||!std::isfinite(f.strategic_speed))f.strategic_speed=22;
    // Treasury arrears on the player civ.
    auto &econ=*std::find_if(exposed.economies.begin(),exposed.economies.end(),
        [&](const CivilizationEconomy &e){return e.civilization_id==civ_id;});
    econ.operating_arrears=25.0;
    const auto ops=inspect_campaign_operations(exposed,9,4.5);
    const auto degraded=std::find_if(ops.begin(),ops.end(),
        [&](const auto &r){return r.event_type=="degraded_structures"&&r.entity_id==worn_site.id;});
    check(degraded!=ops.end()&&std::get<double>(degraded->values.at("degradedCount"))==1.0&&
        std::abs(std::get<double>(degraded->values.at("worstCondition"))-.10)<1e-9,
        "Worn commissioned structure was not reported as degraded.");
    check(std::any_of(ops.begin(),ops.end(),
        [&](const auto &r){return r.event_type=="power_shortfall"&&r.entity_id==worn_site.id;}),
        "Under-powered staffed colony produced no power shortfall.");
    const auto hostile_record=std::find_if(ops.begin(),ops.end(),
        [&](const auto &r){return r.event_type=="foreign_armed_presence"&&r.entity_id==930;});
    check(hostile_record!=ops.end()&&hostile_record->system_id==home_id&&
        std::get<double>(hostile_record->values.at("projectedAttackPerDay"))>0.0,
        "Armed foreign fleet in held space was not reported.");
    check(std::any_of(ops.begin(),ops.end(),
        [&](const auto &r){return r.event_type=="treasury_arrears"&&r.civilization_id==civ_id;}),
        "Carried operating arrears produced no treasury finding.");
    check(std::any_of(ops.begin(),ops.end(),
        [&](const auto &r){return r.event_type=="freight_corridor_gap"&&r.civilization_id==civ_id;}),
        "External import requirement produced no corridor-gap finding.");
    check(std::any_of(ops.begin(),ops.end(),
        [&](const auto &r){return r.event_type=="logistics_critical"&&r.entity_id==frontier.id;}),
        "Under-provisioned external colony was not classified critical.");
    // A marginally under-covered colony lands Strained (coverage in
    // [.70,.95)), not Critical — exercises the other supply band.
    Colony thin;thin.id=next_colony_id++;
    thin.civilization_id=civ_id;thin.system_id=home_id;
    thin.name="Thin margin";thin.kind=SettlementKind::Colony;
    thin.population_millions=10.0;thin.infrastructure=1.5;thin.stability=1.0;
    exposed.colonies.push_back(thin);
    const auto strained_ops=inspect_campaign_operations(exposed,9,4.5);
    check(std::any_of(strained_ops.begin(),strained_ops.end(),
        [&](const auto &r){return r.event_type=="logistics_strained"&&r.entity_id==thin.id;}),
        "Marginally under-covered colony was not classified strained.");
    // A bound below the finding count emits the truncation marker
    // rather than silently dropping records.
    const auto capped_ops=inspect_campaign_operations(exposed,9,4.5,1);
    check(capped_ops.size()==2&&capped_ops.back().event_type=="findings_truncated",
        "Operational finding bound ignored or truncation marker missing.");
  }
  {
    // Treasury depleted: zero balance with a net-negative credit flow.
    // A giant low-stability colony makes services cost dominate any
    // plausible seeded revenue, so the classification is deterministic.
    auto bankrupt=world;
    const int civ_id=world.player_civilization_id;
    const auto civ_it=std::find_if(bankrupt.civilizations.begin(),bankrupt.civilizations.end(),
        [&](const Civilization &c){return c.id==civ_id;});
    const int home_id=civ_it->home_system_id;
    Colony *home_colony=nullptr;
    for(auto &c:bankrupt.colonies)
      if(c.civilization_id==civ_id&&c.system_id==home_id){home_colony=&c;break;}
    check(home_colony,"No home colony to bankrupt.");
    home_colony->population_millions=100000.0;
    home_colony->infrastructure=5.0;home_colony->stability=.1;
    auto &econ=*std::find_if(bankrupt.economies.begin(),bankrupt.economies.end(),
        [&](const CivilizationEconomy &e){return e.civilization_id==civ_id;});
    econ.credits=0.0;econ.operating_arrears=0.0;
    const auto ops=inspect_campaign_operations(bankrupt,9,4.5);
    check(std::any_of(ops.begin(),ops.end(),
        [&](const auto &r){return r.event_type=="treasury_depleted"&&r.civilization_id==civ_id;}),
        "Zero balance with a deficit produced no depleted-treasury finding.");
  }
  auto corrupt=world;corrupt.systems.push_back(corrupt.systems.front());
  corrupt.colonies.front().civilization_id=99999;
  corrupt.economies.front().credits=std::numeric_limits<double>::quiet_NaN();
  const auto faults=inspect_campaign_invariants(corrupt,20,5);
  check(faults.size()==4,"Duplicate ID, orphaned colony, nonfinite credits and the generation-metadata system-count disagreement were not detected.");
  check(faults.front().tick==20&&faults.front().severity==DiagnosticSeverity::Critical,"Invariant lost severity/tick.");
  const auto capped=inspect_campaign_invariants(corrupt,20,5,2);
  check(capped.size()==3&&capped.back().event_type=="findings_truncated",
        "Invariant limit ignored or truncation marker missing.");
  check(rejects([&]{(void)inspect_campaign_invariants(world,0,0,0);}),"Invalid finding bound accepted.");
  {
    // Diplomatic state lives on the runtime, outside FreshCampaignState —
    // its own pass mirrors the snapshot invariant validator and flags
    // campaign-entity refs the validator cannot see.
    const auto clean_diplomacy=inspect_diplomacy_invariants(DiplomacyState{},world,0,0);
    for(const auto &f:clean_diplomacy)std::cerr<<f.subsystem<<" "<<f.event_type<<" "<<f.message<<'\n';
    check(clean_diplomacy.empty(),"Empty diplomatic state failed invariants.");
    DiplomacyStateSnapshot planted;
    DiplomaticContactSnapshot contact;contact.observer_civilization_id=999;
    contact.contact_id="contact-1";contact.target_civilization_id=world.player_civilization_id;
    contact.awareness=ContactAwareness::identified;contact.condition=ContactCondition::active;
    contact.first_observed_tick=1;contact.last_observed_tick=1;
    planted.contacts.push_back(contact);
    TerritorialClaimSnapshot claim;claim.claim_id=1;
    claim.claimant_civilization_id=world.player_civilization_id;claim.system_id=99999;
    claim.asserted_at_tick=1;claim.active=true;planted.claims.push_back(claim);
    DiplomaticHistoryEventSnapshot event;event.event_id=1;event.tick=1;
    event.kind=DiplomaticEventKind::claim_asserted;
    event.primary_civilization_id=world.player_civilization_id;event.system_id=99999;
    event.summary="planted";event.known_to_civilization_ids={world.player_civilization_id};
    planted.recent_history.push_back(event);
    planted.next_claim_id=2;planted.next_agreement_id=1;
    planted.next_proposal_id=1;planted.next_event_id=2;
    auto diplomacy=DiplomacyState::restore(planted);
    const auto dfindings=inspect_diplomacy_invariants(diplomacy,world,20,5);
    for(const auto &f:dfindings)std::cerr<<f.subsystem<<" "<<f.event_type<<" "<<f.entity_id.value_or(-1)<<" "<<f.message<<'\n';
    const auto observers=std::count_if(dfindings.begin(),dfindings.end(),[](const auto &f){return f.event_type=="orphaned_observer";});
    const auto systems=std::count_if(dfindings.begin(),dfindings.end(),[](const auto &f){return f.event_type=="orphaned_system";});
    check(observers==1&&systems==2,"Absent diplomatic observers and systems were not flagged.");
    check(std::none_of(dfindings.begin(),dfindings.end(),[](const auto &f){return f.event_type=="invalid_state";}),
        "Internally-valid diplomatic state failed its own validator.");
  }
  const auto root=fs::path(argv[2])/std::to_string(std::chrono::steady_clock::now().time_since_epoch().count());
  DiagnosticRecord r;r.tick=42;r.game_date="26 Mar 2050";r.real_timestamp=diagnostic_utc_now();
  r.subsystem="research";r.event_type="completed";r.message="Science — completed ☀";r.entity_id=7;
  r.values["funding"]=12.5;r.values["finiteCheck"]=std::numeric_limits<double>::infinity();
  {
    DiagnosticLog log(root/"typed");check(log.append(r),"Normal record filtered.");log.flush();
    std::ifstream in(root/"typed/events-000.jsonl");std::string line;std::getline(in,line);
    const auto j=nlohmann::json::parse(line);check(j["tick"]==42&&j["entityId"]==7&&j["message"]==r.message,
      "Typed Unicode record did not round-trip.");
    check(j["relevantValues"]["funding"]==12.5&&j["relevantValues"]["finiteCheck"]=="Infinity","Nonfinite diagnostics silently became null.");
    bool refused=false;std::thread worker([&]{refused=rejects([&]{log.append(r);});});worker.join();
    check(refused,"Non-owner thread wrote to diagnostic log.");
    check(rejects([&]{DiagnosticLog duplicate(root/"typed");}),"Existing caller-owned directory was reused.");
  }
  {
    DiagnosticLog log(root/"rotated",{DiagnosticDetail::ErrorsOnly,2048,2,1024});
    check(!log.append(r),"Errors-only log accepted info.");r.severity=DiagnosticSeverity::Error;
    for(int i=0;i<30;++i){r.tick=i;r.message=std::string(4000,'x');check(log.append(r),"Error filtered.");}log.flush();
    check(log.written_records()==30&&log.filtered_records()==1&&log.overwritten_segments()>0,"Rotation counters incorrect.");
    int files=0;for(const auto &file:fs::directory_iterator(root/"rotated")){
      ++files;check(file.file_size()<=2048,"Segment exceeded bound.");std::ifstream in(file.path());std::string line;
      while(std::getline(in,line)){const auto j=nlohmann::json::parse(line);check(j["eventType"]=="diagnostic_record_truncated","Oversize diagnostic lacked truncation metadata.");}
    }check(files==2,"Rotation exceeded retained segment bound.");
  }
  check(rejects([&]{DiagnosticLog invalid(root/"invalid",{DiagnosticDetail::Normal,512,0,256});}),"Invalid log policy accepted.");
  {
    DiagnosticBuffer buffer({DiagnosticDetail::Normal,3,4096,1024});
    r.severity=DiagnosticSeverity::Info;r.detail=DiagnosticDetail::Trace;
    check(!buffer.append(r)&&buffer.filtered_records()==1,"Buffer ignored detail level.");
    buffer.set_detail(DiagnosticDetail::Trace);
    for(int i=0;i<20;++i){r.tick=i;r.message=std::string(6000,'x');buffer.append(r);}
    check(buffer.records().size()==3&&buffer.overwritten_records()==17&&buffer.bytes()<=4096,"Buffer exceeded record/byte budget.");
    check(buffer.records().front().record.tick==17&&buffer.records().back().record.tick==19,"Buffer rotation order changed.");
    for(const auto &entry:buffer.records())check(entry.json.size()<=1024&&nlohmann::json::parse(entry.json)["eventType"]=="diagnostic_record_truncated","Buffer bypassed shared JSON truncation.");
    bool refused=false;std::thread worker([&]{refused=rejects([&]{buffer.append(r);});});worker.join();
    check(refused,"Non-owner thread appended history.");
    buffer.clear();check(buffer.records().empty()&&buffer.bytes()==0&&buffer.overwritten_records()==0,"Buffer reset retained prior campaign history.");
  }
  // No test cleanup deletes caller paths. Unique output directories retain the
  // actual records for inspection when a future regression fails.
  std::cout<<"campaign diagnostics tests passed\n";return 0;
}catch(const std::exception &e){std::cerr<<e.what()<<'\n';return 1;}
