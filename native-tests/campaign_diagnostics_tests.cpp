#include <stellar/core/campaign_diagnostics.hpp>
#include <stellar/core/galaxy_catalog.hpp>
#include <stellar/core/persistable_fresh_campaign.hpp>
#include <nlohmann/json.hpp>
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
  check(inspect_campaign_operations(world,0,0).empty(),"Clean world invented an AI stall.");
  auto stalled=world;FleetState fleet;fleet.id=917;fleet.civilization_id=world.player_civilization_id;
  fleet.current_system_id=world.systems.front().id;fleet.fuel_remaining_light_years=8;
  fleet.return_to_base_failure_reason="No owned refuelling settlement is reachable with current fuel.";stalled.fleets.push_back(fleet);
  auto warnings=inspect_campaign_operations(stalled,24,6);
  check(warnings.size()==1&&warnings[0].severity==DiagnosticSeverity::Warning&&warnings[0].entity_id==917&&
      warnings[0].event_type=="return_route_unavailable","Canonical return failure was not reported separately from invariants.");
  check(stalled.fleets.back().fuel_remaining_light_years==8,"Diagnostic repaired fuel.");
  stalled.fleets.back().is_active=false;
  check(inspect_campaign_operations(stalled,24,6).empty(),"Inactive historical ship reported as currently stranded.");
  check(rejects([&]{(void)inspect_campaign_operations(world,0,0,0);}),"Invalid operational finding bound accepted.");
  auto corrupt=world;corrupt.systems.push_back(corrupt.systems.front());
  corrupt.colonies.front().civilization_id=99999;
  corrupt.economies.front().credits=std::numeric_limits<double>::quiet_NaN();
  const auto faults=inspect_campaign_invariants(corrupt,20,5);
  check(faults.size()==3,"Duplicate ID, orphaned colony and nonfinite credits were not detected.");
  check(faults.front().tick==20&&faults.front().severity==DiagnosticSeverity::Critical,"Invariant lost severity/tick.");
  check(inspect_campaign_invariants(corrupt,20,5,2).size()==2,"Invariant limit ignored.");
  check(rejects([&]{(void)inspect_campaign_invariants(world,0,0,0);}),"Invalid finding bound accepted.");
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
