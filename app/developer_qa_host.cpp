#include "developer_qa_host.hpp"
#include "developer_diagnostic_report.hpp"
#include "stellar/build_version.hpp"
#include <stellar/core/campaign_calendar.hpp>
#include <stellar/core/campaign_diagnostics.hpp>
#include <stellar/core/developer_campaign.hpp>
#include <stellar/core/developer_celestial_index.hpp>
#include <stellar/core/galaxy_catalog.hpp>
#include <stellar/core/persistable_fresh_campaign.hpp>
#include <stellar/core/player_campaign_save.hpp>
#include <stellar/engine/atomic_file_write.hpp>
#include <stellar/engine/runtime_paths.hpp>
#include <stellar/engine/sha256.hpp>
#include <nlohmann/json.hpp>
#include <charconv>
#include <chrono>
#include <cmath>
#include <fstream>
#include <iostream>
#include <iomanip>
#include <map>
#include <sstream>
#ifdef _WIN32
#define NOMINMAX
#include <windows.h>
#include <psapi.h>
#endif

namespace {
using namespace stellar::core;
using namespace stellar::engine;
using Json=nlohmann::ordered_json;
namespace fs=std::filesystem;
struct Options {
  fs::path assets=executable_directory(),output,load;
  std::int64_t seed{9142050};std::uint64_t ticks{1440},checkpoint_days{360};
  int systems{500},civilizations{6},ancients{1};std::uint32_t speed{25};
  bool eligible{},headless{},ai{},normal_research{},special_research{},coverage{};
  DiagnosticDetail detail{DiagnosticDetail::Normal};
};
template<class T>T integer(std::string_view value){
  T result{};const auto read=std::from_chars(value.data(),value.data()+value.size(),result);
  if(read.ec!=std::errc{}||read.ptr!=value.data()+value.size())throw std::invalid_argument("Expected an integer argument.");
  return result;
}
Options parse(int argc,char **argv){
  Options o;bool length=false,creating=false;
  for(int i=1;i<argc;++i){
    const std::string_view key=argv[i];
    if(key=="--developer-qa")continue;
    if(key=="--devtools"){o.eligible=true;continue;}
    if(key=="--headless"){o.headless=true;continue;}
    if(key=="--full-celestial-coverage"){o.coverage=true;creating=true;continue;}
    if(key=="--ai-control"){o.ai=true;continue;}
    if(key=="--all-normal-research"){o.normal_research=true;continue;}
    if(key=="--all-special-research"){o.special_research=true;continue;}
    if(i+1>=argc)throw std::invalid_argument("Missing value for "+std::string(key));
    const std::string_view value=argv[++i];
    if(key=="--asset-root")o.assets=fs::path(value);
    else if(key=="--output")o.output=fs::path(value);
    else if(key=="--load")o.load=fs::path(value);
    else if(key=="--seed"){o.seed=integer<std::int64_t>(value);creating=true;}
    else if(key=="--systems"){o.systems=integer<int>(value);creating=true;}
    else if(key=="--civilizations"){o.civilizations=integer<int>(value);creating=true;}
    else if(key=="--ancients"){o.ancients=integer<int>(value);creating=true;}
    else if(key=="--speed")o.speed=integer<std::uint32_t>(value);
    else if(key=="--checkpoint-days")o.checkpoint_days=integer<std::uint64_t>(value);
    else if(key=="--ticks"||key=="--years"){
      if(length)throw std::invalid_argument("Use one duration option.");length=true;o.ticks=integer<std::uint64_t>(value);
      if(key=="--years"){if(o.ticks>1000)throw std::invalid_argument("QA duration exceeds 1000 years.");o.ticks*=1440;}
    }else if(key=="--log-level"){
      if(value=="errors")o.detail=DiagnosticDetail::ErrorsOnly;
      else if(value=="normal")o.detail=DiagnosticDetail::Normal;
      else if(value=="detailed")o.detail=DiagnosticDetail::Detailed;
      else if(value=="trace")o.detail=DiagnosticDetail::Trace;
      else throw std::invalid_argument("Unknown diagnostic log level.");
    }else throw std::invalid_argument("Unknown developer QA option: "+std::string(key));
  }
  if(!o.eligible||!o.headless)throw std::invalid_argument("QA execution requires explicit --devtools --headless entitlement.");
  if(o.output.empty()||fs::exists(o.output))throw std::invalid_argument("Choose a new --output directory; existing data is never overwritten.");
  if(o.ticks==0||o.ticks>1440000||o.checkpoint_days<1||o.checkpoint_days>36000)throw std::invalid_argument("QA duration/checkpoint interval is outside supported bounds.");
  if(o.systems!=250&&o.systems!=500&&o.systems!=1000&&o.systems!=2500)throw std::invalid_argument("Galaxy size must be 250, 500, 1000 or 2500.");
  if(o.civilizations<1||o.civilizations>32||o.ancients<0||o.ancients>8)throw std::invalid_argument("Unsupported civilization count.");
  if(o.speed!=1&&o.speed!=2&&o.speed!=5&&o.speed!=10&&o.speed!=25)throw std::invalid_argument("QA speed must be 1, 2, 5, 10 or 25.");
  if(!o.load.empty()&&creating)throw std::invalid_argument("Generation settings cannot replace a loaded checkpoint.");
  return o;
}
std::string read(const fs::path &path){
  if(fs::file_size(path)>512ULL*1024*1024)throw std::invalid_argument("Checkpoint exceeds 512 MiB.");
  std::ifstream file(path,std::ios::binary);if(!file)throw std::runtime_error("Cannot read checkpoint.");
  return {std::istreambuf_iterator<char>(file),std::istreambuf_iterator<char>()};
}
void write(const fs::path &path,std::string_view text){
  write_file_atomically(path,std::as_bytes(std::span(text.data(),text.size())));
}
std::uint64_t memory_bytes(){
#ifdef _WIN32
  PROCESS_MEMORY_COUNTERS_EX memory{};memory.cb=sizeof(memory);
  if(GetProcessMemoryInfo(GetCurrentProcess(),reinterpret_cast<PROCESS_MEMORY_COUNTERS*>(&memory),sizeof(memory)))return memory.PrivateUsage;
#endif
  return 0;
}
CampaignFrame create(const Options &o,const std::string &created){
  const auto research=o.assets/"Data/research/v1";
  if(!o.load.empty()){
    if(!is_developer_campaign_save_path(o.load))throw std::invalid_argument("QA resumes only marked .dev17.json checkpoints.");
    auto restored=restore_developer_campaign_json(load_adaptive_research_strategic_runtime(research),read(o.load));
    StrategicClock clock;clock.restore(restored.simulation_days());
    return CampaignFrame(std::move(restored).activate(),clock,CampaignFramePolicy::Developer);
  }
  auto world=seed_persistable_fresh_campaign(o.seed,load_nearby_catalog(o.assets/"Data/astronomy/hyg-nearby-500-v1.json"),
      {created,o.systems,o.civilizations,o.ancients,"terran_baseline",StellarPopulationOptions{},o.coverage});
  if(!world.developer_provenance)world.developer_provenance=CampaignDeveloperProvenance{};
  return CampaignFrame(IntegratedAdaptiveCampaignRuntime::create_fresh(load_adaptive_research_strategic_runtime(research),std::move(world)),{},CampaignFramePolicy::Developer);
}
}

int run_developer_qa(int argc,char **argv){
  const auto options=parse(argc,argv);const auto started=diagnostic_utc_now();
  auto frame=create(options,started);auto &world=frame.runtime().world().campaign();
  if(options.ai)set_developer_ai_control(frame.runtime(),true);
  const auto research_setup=initialize_developer_research(frame.runtime(),{options.normal_research,options.special_research});
  frame.set_developer_speed(options.speed);frame.set_profiling_enabled(true);
  frame.clock().set_speed(StrategicSpeed::Normal);frame.set_tactical_speed(options.speed);
  if(!fs::create_directories(options.output))throw std::runtime_error("Output directory already exists.");
  DiagnosticLog log(options.output/"logs",{options.detail});
  const auto start_day=frame.clock().simulation_days();
  const auto initial_ticks=world.developer_provenance->simulation.completed_ticks+world.developer_provenance->simulation.tactical_completed_ticks;
  std::uint64_t completed{},invariant_checks{},critical{},checkpoints{},profiled{},total_ns{},maximum_ns{},peak_memory{},first_memory{};
  std::map<std::string,std::uint64_t> event_counts;
  std::string failure;bool roundtrip=false;
  const auto begin=std::chrono::steady_clock::now();
  const auto record=[&](std::string type,std::string message,DiagnosticSeverity severity=DiagnosticSeverity::Info){
    DiagnosticRecord r;r.tick=initial_ticks+completed;r.game_date=format_campaign_date(frame.clock().simulation_days());
    r.real_timestamp=diagnostic_utc_now();r.subsystem="qa";r.event_type=std::move(type);r.message=std::move(message);r.severity=severity;log.append(std::move(r));
  };
  const auto checkpoint=[&](std::string filename){
    auto data=PreparedPlayerCampaignSave::capture_developer(frame.runtime(),{frame.clock().simulation_days(),STELLAR_GAME_VERSION,started});
    write_prepared_campaign(options.output/filename,data,false);++checkpoints;
    record("checkpoint",filename);
  };
  Json args=Json::array();for(int i=1;i<argc;++i)args.push_back(argv[i]);
  Json metadata={{"schemaVersion",1},{"mode",world.developer_provenance->full_celestial_coverage?"content_coverage_qa":"natural_generation_qa"},{"developerSession",true},
      {"gameVersion",STELLAR_GAME_VERSION},{"engineVersion",STELLAR_ENGINE_VERSION},{"sourceCommit",STELLAR_SOURCE_COMMIT},
      {"executableSha256",stellar::app_diagnostics::executable_fingerprint(executable_path())},
      {"startedUtc",started},{"seed",world.seed},{"systemCount",world.systems.size()},
      {"startDay",start_day},{"requestedAdditionalTicks",options.ticks},{"fixedStrategicDays",.25},
      {"fixedTacticalSeconds",.1},{"speed",options.speed},{"playerAiControl",world.developer_provenance->player_ai_control},
      {"normalResearchEstablished",research_setup.completed_normal},{"specialResearchEstablished",research_setup.completed_special},
      {"forcedCelestialCoverage",world.developer_provenance->full_celestial_coverage},{"arguments",args}};
  write(options.output/"session.json",metadata.dump(2));
  write(options.output/"README.md","# Stellar Continuum developer QA session\n\n"
    "This folder contains real campaign checkpoints and bounded diagnostic logs.\n"
    "Developer saves cannot replace ordinary player saves. `session.json` records\n"
    "the seed, build and exact arguments. `initial.dev17.json` is the exact starting\n"
    "world, including pending simulation and AI decisions. Resume with the same\n"
    "executable using --developer-qa --headless --devtools --load PATH --output NEWDIR.\n"
    "The three periodic slots and their backups rotate; logs retain eight 4 MiB\n"
    "segments. `summary.json` reports any data overwritten by rotation.\n"
    "Natural generation does not force rare objects, fund empires or reveal fog.\n"
    "Calendar dates begin 21 March 2050; duration labels use 360-day years.\n"
    "Execution timing and process private-memory measurements are observations,\n"
    "not simulation inputs. No rendering or frame-rate result is measured here.\n"
    "The ZIP in exports contains session.json (build/settings), summary.md/json (results),\n"
    "commands.json (invocation and initial checkpoint), celestial-index.json/coverage.txt\n"
    "(population), performance.csv (measured phases), initial/latest/retained periodic\n"
    "developer checkpoints, and retained logs/events-*.jsonl (typed events/findings).\n"
    "A critical checkpoint is included when produced. Full command replay is not yet available.\n");
  write(options.output/"commands.json",Json{{"schemaVersion",1},{"arguments",args},{"initialCheckpoint","initial.dev17.json"}}.dump(2));
  write(options.output/"celestial-index.json",stellar::app_diagnostics::coverage_json(world));
  if(world.generation_metadata){
    const auto &g=*world.generation_metadata;
    metadata["generatorVersion"]=g.generator_version;
    metadata["galaxyShape"]=g.galaxy_shape;
    metadata["stellarProfileVersion"]=g.stellar_profile_version.value_or("legacy");
    write(options.output/"session.json",metadata.dump(2));
    write(options.output/"celestial-coverage.txt",stellar_population_diagnostics(world.systems,world.bodies,
        g.stellar_population.value_or(StellarPopulationOptions{}),
        world.galactic_core?world.galactic_core->black_hole:std::nullopt,static_cast<std::uint64_t>(world.seed)));
  }
  const auto inspect=[&]{
    ++invariant_checks;auto findings=inspect_campaign_invariants(world,initial_ticks+completed,frame.clock().simulation_days());
    for(auto &finding:findings){finding.real_timestamp=diagnostic_utc_now();log.append(std::move(finding));++critical;}
    if(critical)throw std::runtime_error("Campaign invariant violation; inspect structured logs.");
  };
  auto next_check=std::floor(start_day)+1.,next_sample=start_day,next_checkpoint=start_day+options.checkpoint_days;
  std::uint64_t slot{};
  try{
    inspect();checkpoint("initial.dev17.json");first_memory=memory_bytes();peak_memory=first_memory;
    while(completed<options.ticks){
      const auto &sim=world.developer_provenance->simulation;const auto before=sim.completed_ticks+sim.tactical_completed_ticks;
      const bool tactical=world.active_combat_encounter&&!world.active_combat_encounter->reconciled;
      const auto remaining=options.ticks-completed;
      CampaignFrameResult result;
      if(remaining<(tactical?32u:8u)&&frame.developer_ticks_behind()){
        // A loaded debt can exceed the final requested batch. Single-step
        // the last few ticks without consuming beyond the requested endpoint.
        if(tactical)frame.set_tactical_speed(0);else frame.clock().set_speed(StrategicSpeed::Paused);
        result=frame.step_developer();
      }else{
        if(tactical)frame.set_tactical_speed(options.speed);else frame.clock().set_speed(StrategicSpeed::Normal);
        const auto desired=std::min<std::uint64_t>(remaining,4);
        result=frame.advance(frame.developer_ticks_behind()?0.:
            static_cast<double>(desired)*(tactical?.1:.25)/options.speed);
      }
      completed+=sim.completed_ticks+sim.tactical_completed_ticks-before;
      if(sim.completed_ticks+sim.tactical_completed_ticks==before)throw std::runtime_error("Simulation made no progress.");
      for(auto ns:result.tick_execution_nanoseconds){++profiled;total_ns+=ns;maximum_ns=std::max(maximum_ns,ns);}
      for(std::size_t i=0;i<result.strategic_results.size();++i){
        for(auto &r:campaign_step_diagnostics(result.strategic_results[i],before+i+1,result.completed_end_days[i])){
          ++event_counts[r.subsystem+"/"+r.event_type];r.real_timestamp=diagnostic_utc_now();log.append(std::move(r));
        }
      }
      for(const auto &e:result.tactical_events){++event_counts["combat/tactical_event"];record("tactical_event",e.message);}
      const auto day=frame.clock().simulation_days();
      if(day>=next_check||tactical){inspect();next_check=std::floor(day)+1.;}
      if(day>=next_sample){
        const auto memory=memory_bytes();peak_memory=std::max(peak_memory,memory);
        DiagnosticRecord r;r.tick=initial_ticks+completed;r.game_date=format_campaign_date(day);r.real_timestamp=diagnostic_utc_now();
        r.subsystem="performance";r.event_type="simulation_sample";r.message="Observed campaign state.";
        r.values["privateMemoryBytes"]=memory;r.values["fleets"]=static_cast<std::uint64_t>(world.fleets.size());
        r.values["colonies"]=static_cast<std::uint64_t>(world.colonies.size());r.values["pendingTicks"]=frame.developer_ticks_behind();
        log.append(std::move(r));next_sample=day+30.;
      }
      if(day>=next_checkpoint){checkpoint("periodic-"+std::to_string(slot++%3)+".dev17.json");next_checkpoint=day+options.checkpoint_days;
        std::cout<<format_campaign_date(day)<<" | "<<completed<<" ticks | "<<event_counts["research/research_outcome"]<<" research outcomes\n"<<std::flush;}
    }
    inspect();checkpoint("latest.dev17.json");
    const auto source=read(options.output/"latest.dev17.json");
    auto restored=restore_developer_campaign_json(load_adaptive_research_strategic_runtime(options.assets/"Data/research/v1"),source);
    const auto day=restored.simulation_days();auto runtime=std::move(restored).activate();
    const auto original=nlohmann::json::parse(source);
    const auto reloaded=nlohmann::json::parse(capture_developer_campaign_json(runtime,{day,STELLAR_GAME_VERSION,started}));
    roundtrip=original==reloaded;
    if(!roundtrip){
      auto difference=nlohmann::json::diff(original,reloaded);
      if(difference.size()>128)difference.erase(difference.begin()+128,difference.end());
      write(options.output/"checkpoint-difference.json",difference.dump(2));
    }
    if(!roundtrip)throw std::runtime_error("Final checkpoint did not round-trip exactly.");
    record("completed","Requested authoritative ticks completed; checkpoint round-trip passed.");
  }catch(const std::exception &e){
    failure=e.what();++critical;record("critical_failure",failure,DiagnosticSeverity::Critical);
    try{checkpoint("critical.dev17.json");}catch(const std::exception &capture){record("critical_capture_failed",capture.what(),DiagnosticSeverity::Critical);}
  }
  Json operational_findings=Json::array();
  for(auto finding:inspect_campaign_operations(world,initial_ticks+completed,frame.clock().simulation_days())){
    operational_findings.push_back({{"eventType",finding.event_type},{"entityId",finding.entity_id.value_or(-1)},
        {"civilizationId",finding.civilization_id.value_or(-1)},{"systemId",finding.system_id.value_or(-1)},{"message",finding.message}});
    finding.real_timestamp=diagnostic_utc_now();log.append(std::move(finding));
  }
  log.flush();
  const auto final_memory=memory_bytes();peak_memory=std::max(peak_memory,final_memory);
  Json phases=Json::object();
  for(const auto &p:frame.runtime().performance_samples())phases[p.phase]={
      {"samples",p.timing.samples},{"totalMilliseconds",p.timing.total_nanoseconds/1e6},
      {"meanMilliseconds",p.timing.samples?static_cast<double>(p.timing.total_nanoseconds)/p.timing.samples/1e6:0.},
      {"maximumMilliseconds",p.timing.maximum_nanoseconds/1e6}};
  const auto elapsed=std::chrono::duration<double>(std::chrono::steady_clock::now()-begin).count();
  Json summary={{"schemaVersion",1},{"passed",failure.empty()},{"failure",failure},{"completedTicks",completed},
      {"startDay",start_day},{"endDay",frame.clock().simulation_days()},{"endDate",format_campaign_date(frame.clock().simulation_days())},
      {"wallSeconds",elapsed},{"profiledTicks",profiled},{"meanTickMilliseconds",profiled?static_cast<double>(total_ns)/profiled/1e6:0.},
      {"worstTickMilliseconds",static_cast<double>(maximum_ns)/1e6},{"initialPrivateMemoryBytes",first_memory},
      {"peakPrivateMemoryBytes",peak_memory},{"finalPrivateMemoryBytes",final_memory},{"invariantChecks",invariant_checks},
      {"criticalFindings",critical},{"operationalFindings",operational_findings},{"checkpointsWritten",checkpoints},{"finalCheckpointRoundTrip",roundtrip},{"phases",phases},
      {"events",event_counts},{"loggedRecords",log.written_records()},{"filteredRecords",log.filtered_records()},
      {"overwrittenLogSegments",log.overwritten_segments()},{"coverageForced",world.developer_provenance->full_celestial_coverage}};
  write(options.output/"summary.json",summary.dump(2));
  write(options.output/"summary.md",std::string("# Developer QA result\n\n")+(failure.empty()?"PASS":"FAIL")+" — "+std::to_string(completed)+
      " completed ticks; "+format_campaign_date(frame.clock().simulation_days())+".\n\n"+failure+
      "\n\nUnresolved operational findings: "+std::to_string(operational_findings.size())+". A passing invariant/reload check does not certify AI effectiveness."+
      "\n\nSee summary.json for measured timing, memory, canonical event counts and checkpoint validation.\n");
  write(options.output/"performance.csv",stellar::app_diagnostics::performance_csv(frame));
  const auto bundle=stellar::app_diagnostics::export_qa_directory(options.output,world.seed,started);
  std::cout<<"Diagnostic bundle: "<<bundle.string()<<'\n';
  std::cout<<summary.dump(2)<<'\n';return failure.empty()?0:1;
}
