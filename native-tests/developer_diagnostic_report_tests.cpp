#include "developer_diagnostic_report.hpp"
#include "campaign_diagnostic_monitor.hpp"
#include "diagnostic_zip_test_reader.hpp"
#include <stellar/core/persistable_fresh_campaign.hpp>
#include <stellar/engine/memory_tracker.hpp>
#include <stellar/engine/runtime_paths.hpp>
#include <chrono>
#include <iostream>
#include <limits>

namespace fs=std::filesystem;
using namespace stellar::core;
using namespace stellar::app_diagnostics;
void check(bool b,const char *s){if(!b)throw std::runtime_error(s);}
int main(int argc,char **argv)try{
  check(argc==4,"Expected catalog, research and scratch root.");
  auto world=seed_persistable_fresh_campaign(-42,load_nearby_catalog(argv[1]),
      {"2050-03-21T00:00:00Z",250,3,0,"terran_baseline",StellarPopulationOptions{},true});
  CampaignFrame frame(IntegratedAdaptiveCampaignRuntime::create_fresh(load_adaptive_research_strategic_runtime(argv[2]),std::move(world)),{},CampaignFramePolicy::Developer);
  frame.set_developer_speed(1);
  const std::string stamp="2026-09-17T12:34:56Z";
  const auto hash=executable_fingerprint(stellar::engine::executable_path());
  check(hash.size()==64,"Executable digest missing.");
  const auto before=capture_developer_campaign_json(frame.runtime(),{0,"test",stamp});
  CampaignDiagnosticMonitor monitor;
  monitor.observe(frame,{},stamp);
  // Seeded campaigns can carry genuine operational findings (e.g.
  // sustenance shortfalls); init is proven by the check counter and the
  // monitor-start record, not by an empty finding list.
  check(monitor.invariant_checks()==1&&!monitor.history().records().empty()&&
        monitor.history().records().front().record.event_type=="native_monitor_started",
        "Native monitor did not initialize with current-state inspection.");
  // The research pass replays the codec's save-path validation on live
  // state — a fresh campaign must produce no false-positive findings.
  const auto research_findings=inspect_research_invariants(
      frame.runtime().research(),frame.runtime().research_runtime(),
      frame.runtime().world().campaign(),0,0);
  for(const auto &f:research_findings)std::cerr<<f.subsystem<<" "<<f.event_type<<" "<<f.message<<'\n';
  check(research_findings.empty(),"Fresh research state failed invariants.");
  // The continuation pass replays the save-path schedule validation on a
  // live capture — a fresh runtime must produce no findings.
  const auto continuation_findings=inspect_continuation_invariants(frame.runtime(),0,0);
  for(const auto &f:continuation_findings)std::cerr<<f.subsystem<<" "<<f.event_type<<" "<<f.message<<'\n';
  check(continuation_findings.empty(),"Fresh runtime continuation failed invariants.");
  check(std::none_of(monitor.history().records().begin(),monitor.history().records().end(),
        [](const auto &r){return r.record.subsystem=="research"||r.record.subsystem=="diplomacy"||r.record.subsystem=="continuation";}),
        "Monitor reported false-positive research/diplomacy/continuation findings.");
  const auto initialized_records=monitor.history().records().size();
  monitor.observe(frame,{},stamp);check(monitor.invariant_checks()==1&&monitor.history().records().size()==initialized_records,"Paused rendering spammed diagnostics.");
  const auto memory_id=stellar::engine::MemoryTracker::instance().register_subsystem("test-subsystem");
  stellar::engine::MemoryTracker::instance().report(memory_id,1234,4096);
  const auto entries=capture_developer_report(frame,{"test","engine-test","commit-test",hash},stamp,&monitor.history());
  check(before==capture_developer_campaign_json(frame.runtime(),{0,"test",stamp}),"Report modified authoritative campaign.");
  const auto root=fs::path(argv[3])/std::to_string(std::chrono::steady_clock::now().time_since_epoch().count());
  const auto archive=stellar::engine::write_diagnostic_bundle(root,archive_name(stamp,-42),entries);
  const auto files=diagnostic_zip_test::unzip(archive);
  check(files.at("latest.dev17.json")==before&&!files.contains("campaign.player17.json"),"Checkpoint altered or mislabeled.");
  const auto metadata=Json::parse(files.at("session.json"));
  check(!files.at("native-events.jsonl").empty()&&metadata["nativeHistory"]["retainedRecords"]==initialized_records,"Historical native records omitted.");
  check(metadata["seed"]==-42&&metadata["forcedCelestialCoverage"]==true&&metadata["executableSha256"]==hash,"Wrong provenance in report.");
  check(!Json::parse(files.at("replay.json"))["fullCommandReplayAvailable"].get<bool>(),"Report promised unsupported command replay.");
  const auto restored=restore_developer_campaign_json(load_adaptive_research_strategic_runtime(argv[2]),files.at("latest.dev17.json"));
  check(restored.simulation_days()==0,"Exported checkpoint cannot be restored.");
  check(Json::parse(files.at("coverage.json"))["objects"].size()>=250,"Coverage omitted real celestial objects.");
  check(files.at("errors.jsonl").empty()&&files.at("performance.csv").starts_with("phase,samples,"),"Findings/performance artifacts missing.");
  const auto memory_json=Json::parse(files.at("memory.json"));
  bool tracked=false;for(const auto &s:memory_json["subsystems"])if(s["name"]=="test-subsystem"&&s["currentBytes"]==1234&&s["reservedBytes"]==4096)tracked=true;
  check(tracked,"Diagnostic bundle omitted the tracked subsystem memory report.");
  // Resume after capture; the exported bytes must remain at their capture tick.
  frame.set_profiling_enabled(true);frame.clock().set_speed(StrategicSpeed::Normal);
  const auto result=frame.advance(.25);monitor.observe(frame,result,stamp);
  check(monitor.invariant_checks()==1,"Fractional-day advance repeated expensive invariant scan.");
  const auto next=frame.advance(.75);
  auto &live=frame.runtime().world().campaign();FleetState fixture;fixture.id=917;fixture.civilization_id=live.player_civilization_id;
  fixture.current_system_id=live.systems.front().id;fixture.fuel_remaining_light_years=8;
  fixture.return_to_base_failure_reason="Test fixture recovery warning";live.fleets.push_back(fixture);
  monitor.observe(frame,next,stamp);
  check(monitor.invariant_checks()==2,"Native monitor missed new simulation day.");
  bool warning=false;for(const auto &r:monitor.history().records())if(r.record.event_type=="return_route_unavailable")warning=true;
  check(warning,"Native monitor discarded actual operational failure.");
  const auto count=monitor.history().records().size();monitor.observe(frame,{},stamp);
  check(monitor.history().records().size()==count,"Identical warning duplicated on paused frame.");
  live.fleets.pop_back();
  check(capture_developer_campaign_json(frame.runtime(),{frame.clock().simulation_days(),"test",stamp})!=before&&files.at("latest.dev17.json")==before,"Report retained live mutable state.");
  // Identical checkpoints with and without live monitoring must evolve identically.
  const auto resume=[&]{auto loaded=restore_developer_campaign_json(load_adaptive_research_strategic_runtime(argv[2]),before);
    StrategicClock clock;clock.restore(loaded.simulation_days());
    CampaignFrame f(std::move(loaded).activate(),clock,CampaignFramePolicy::Developer);f.set_developer_speed(25);f.clock().set_speed(StrategicSpeed::Normal);return f;};
  auto measured=resume(),baseline=resume();CampaignDiagnosticMonitor observed;
  for(int i=0;i<40;++i){const auto step=measured.advance(.04);observed.observe(measured,step,stamp);(void)baseline.advance(.04);}
  check(observed.invariant_checks()>1&&capture_developer_campaign_json(measured.runtime(),{measured.clock().simulation_days(),"test",stamp})==
      capture_developer_campaign_json(baseline.runtime(),{baseline.clock().simulation_days(),"test",stamp}),"Diagnostic observation changed seeded simulation results.");
  auto ordinary=seed_persistable_fresh_campaign(42,load_nearby_catalog(argv[1]),{"now",250,3,0});
  CampaignFrame normal(IntegratedAdaptiveCampaignRuntime::create_fresh(load_adaptive_research_strategic_runtime(argv[2]),std::move(ordinary)),{},CampaignFramePolicy::Player);
  bool refused=false;try{(void)capture_developer_report(normal,{"test","test","test",hash},stamp);}catch(const std::exception&){refused=true;}
  check(refused,"Normal campaign allowed developer report capture.");
  CampaignDiagnosticMonitor disabled;disabled.observe(normal,{},stamp);check(disabled.history().records().empty()&&disabled.invariant_checks()==0,"Normal game ran hidden diagnostics.");
  // Broken campaign timestamps cannot be serialized; diagnostics must survive
  // without claiming that an absent checkpoint can be replayed.
  auto broken=capture_developer_report(frame,{"test","engine-test","commit-test",hash},"invalid timestamp",&monitor.history());
  std::map<std::string,std::string> partial;for(auto &e:broken)partial.emplace(std::move(e.name),std::move(e.bytes));
  check(partial.contains("checkpoint-error.txt")&&!partial.contains("latest.dev17.json")&&
      Json::parse(partial.at("session.json"))["checkpointIncluded"]==false&&
      Json::parse(partial.at("replay.json"))["continuationSupported"]==false,"Failed checkpoint capture discarded evidence or promised replay.");
  monitor.reset();check(monitor.history().records().empty()&&monitor.invariant_checks()==0,"Monitor leaked history across campaigns.");
  {std::error_code cleanup;fs::remove_all(root,cleanup);}// retain run dir only on failure
  std::cout<<"Developer diagnostic snapshot/checkpoint/archive checks passed\n";return 0;
}catch(const std::exception &e){std::cerr<<e.what()<<'\n';return 1;}
