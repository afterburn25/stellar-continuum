#pragma once
#include <stellar/core/campaign_frame.hpp>
#include <stellar/core/campaign_calendar.hpp>
#include <stellar/core/campaign_diagnostics.hpp>
#include <stellar/core/campaign_world_projection.hpp>
#include <stellar/core/developer_campaign.hpp>
#include <stellar/core/developer_celestial_index.hpp>
#include <stellar/engine/diagnostic_bundle.hpp>
#include <stellar/engine/memory_tracker.hpp>
#include <stellar/engine/sha256.hpp>
#include <nlohmann/json.hpp>
#include <iomanip>
#include <sstream>

namespace stellar::app_diagnostics {
using Json=nlohmann::ordered_json;
using Entry=stellar::engine::DiagnosticBundleEntry;
struct BuildContext {std::string game_version,engine_version,source_commit,executable_sha256;};
inline std::string executable_fingerprint(const std::filesystem::path &path){
  const auto bytes=stellar::engine::read_diagnostic_file(path,512u*1024u*1024u);
  const auto digest=stellar::engine::sha256(std::span(reinterpret_cast<const std::uint8_t*>(bytes.data()),bytes.size()));
  std::ostringstream out;out<<std::hex<<std::setfill('0');for(auto b:digest)out<<std::setw(2)<<static_cast<unsigned>(b);return out.str();
}
inline std::string archive_name(std::string_view timestamp,std::int64_t seed){
  std::string date;for(char c:timestamp)if(c>='0'&&c<='9')date+=c;
  if(date.empty())throw std::invalid_argument("Diagnostic timestamp is missing.");
  return "Stellar-Continuum-Diagnostic-"+date+"-"+std::to_string(seed)+".zip";
}
inline std::string performance_csv(stellar::core::CampaignFrame &frame){
  std::ostringstream csv;csv.imbue(std::locale::classic());
  csv<<"phase,samples,total_ms,mean_ms,maximum_ms\n";
  for(const auto &p:frame.runtime().performance_samples())csv<<p.phase<<','<<p.timing.samples<<','
      <<p.timing.total_nanoseconds/1e6<<','<<(p.timing.samples?static_cast<double>(p.timing.total_nanoseconds)/p.timing.samples/1e6:0.)
      <<','<<p.timing.maximum_nanoseconds/1e6<<'\n';
  return csv.str();
}
inline std::string coverage_json(const stellar::core::FreshCampaignState &world){
  const auto index=stellar::core::build_developer_celestial_index(world);Json counts=Json::array(),objects=Json::array();
  for(const auto &c:index.counts)counts.push_back({{"typeId",c.type_id},{"name",c.type_name},{"natural",c.natural},{"forced",c.forced},{"total",c.natural+c.forced}});
  for(const auto &e:index.entries){
    Json object={{"key",e.key},{"name",e.name},{"typeId",e.type_id},{"region",e.region},{"x",e.x},{"y",e.y},
      {"devCoverageForced",e.forced},{"rare",e.rare},{"central",e.central},{"massSolar",e.mass_solar}};
    if(e.system_id)object["systemId"]=*e.system_id;objects.push_back(std::move(object));
  }
  return Json{{"schemaVersion",1},{"counts",counts},{"objects",objects}}.dump(2);
}
// Owner-thread capture. No mutation, simulation advancement, normal saves or
// deferred references. The returned immutable bytes may be written by a worker.
inline std::vector<Entry> capture_developer_report(stellar::core::CampaignFrame &frame,const BuildContext &build,std::string timestamp,const stellar::engine::DiagnosticBuffer *history=nullptr){
  using namespace stellar::core;
  const auto &world=frame.runtime().world().campaign();
  if(!world.developer_provenance)throw std::logic_error("Developer report requires an isolated developer campaign.");
  const auto &dev=*world.developer_provenance;
  const auto tick=dev.simulation.completed_ticks+dev.simulation.tactical_completed_ticks;
  const auto day=frame.clock().simulation_days();
  Json metadata={{"schemaVersion",1},{"developerSession",true},{"mode","native_developer_snapshot"},
    {"capturedUtc",timestamp},{"gameVersion",build.game_version},{"engineVersion",build.engine_version},
    {"sourceCommit",build.source_commit},{"executableSha256",build.executable_sha256},{"seed",world.seed},
    {"systemCount",world.systems.size()},{"simulationDay",day},{"gameDate",format_campaign_date(day)},
    {"tick",tick},{"playerAiControl",dev.player_ai_control},{"forcedCelestialCoverage",dev.full_celestial_coverage},
    {"speed",dev.simulation.speed},{"fixedStrategicDays",.25},{"fixedTacticalSeconds",.1}};
  // Typed-store bridge census: the read-only projection carries the
  // whole campaign into engine::World — entity/tag counts and
  // legacy/parent resolution make the bridge measurable over real
  // state rather than a mock fixture.
  const auto projected=campaign_world_projection_census(world);
  metadata["worldProjection"]={{"entities",projected.entities},{"systems",projected.systems},
    {"bodies",projected.bodies},{"civilizations",projected.civilizations},{"colonies",projected.colonies},
    {"fleets",projected.fleets},{"economies",projected.economies},{"technologies",projected.technologies},
    {"construction",projected.construction},{"shipyards",projected.shipyards},
    {"legacyBound",projected.legacy_bound},
    {"parented",projected.parented},{"unparented",projected.unparented}};
  if(world.generation_metadata){const auto &g=*world.generation_metadata;metadata["generatorVersion"]=g.generator_version;
    metadata["galaxyShape"]=g.galaxy_shape;metadata["stellarProfileVersion"]=g.stellar_profile_version.value_or("legacy");}
  if(world.generation_metadata&&world.generation_metadata->configuration){
    const auto& c=*world.generation_metadata->configuration;
    metadata["generationConfiguration"]={{"baseSeed",c.base_seed},{"fingerprint",c.fingerprint},{"morphology",galaxy_morphology_id(c.morphology)},
      {"requestedPopulation",population_selection_id(c.requested_population)},{"resolvedPopulation",population_state_name(c.resolved_population)},
      {"systems",c.system_count},{"preWarpCivilizations",c.pre_warp_count},{"ancientCivilizations",c.ancient_count},{"species",c.player_species_id},
      {"developerCoverage",c.developer_full_coverage},{"assetSetVersion",c.asset_set_version},{"previewAsset",c.preview_asset_id},{"mapAsset",c.map_asset_id}};
  }
  auto findings=inspect_campaign_invariants(world,tick,day);
  const auto critical=findings.size();
  auto operations=inspect_campaign_operations(world,tick,day);
  const auto warnings=operations.size();findings.insert(findings.end(),operations.begin(),operations.end());
  auto diplomatic=inspect_diplomacy_invariants(frame.runtime().diplomacy(),world,tick,day);
  findings.insert(findings.end(),diplomatic.begin(),diplomatic.end());
  auto research=inspect_research_invariants(frame.runtime().research(),frame.runtime().research_runtime(),world,tick,day);
  findings.insert(findings.end(),research.begin(),research.end());
  std::string errors,warning_log,diagnostics;
  for(auto &f:findings){f.real_timestamp=timestamp;const auto line=stellar::engine::diagnostic_record_json(f);diagnostics+=line;
    if(f.severity==stellar::engine::DiagnosticSeverity::Warning)warning_log+=line;else errors+=line;}
  std::string checkpoint,checkpoint_error;
  try{checkpoint=capture_developer_campaign_json(frame.runtime(),{day,build.game_version,timestamp});}
  catch(const std::exception &error){
    const std::string_view message=error.what();auto count=std::min<std::size_t>(message.size(),2048);
    if(count<message.size())while(count&&(static_cast<unsigned char>(message[count])&0xc0)==0x80)--count;
    checkpoint_error=message.substr(0,count);
  }
  metadata["checkpointIncluded"]=!checkpoint.empty();
  if(checkpoint.empty())metadata["checkpointError"]=checkpoint_error;
  Json replay={{"schemaVersion",1},{"checkpoint",checkpoint.empty()?"":"latest.dev17.json"},{"continuationSupported",!checkpoint.empty()},
    {"fullCommandReplayAvailable",false},{"requiredExecutableSha256",build.executable_sha256},
    {"instructions","Resume this checkpoint with the same build: --developer-qa --headless --devtools --load latest.dev17.json --ticks 1440 --output NEW_DIRECTORY. Omit research overrides to preserve its exact state."}};
  if(checkpoint.empty())replay["instructions"]="The current state could not be encoded. Inspect checkpoint-error.txt and the typed findings, then use a prior healthy developer checkpoint from the same build. No replacement or repaired checkpoint was invented.";
  std::string retained;
  if(history){
    metadata["nativeHistory"]={{"retainedRecords",history->records().size()},{"overwrittenRecords",history->overwritten_records()},
      {"filteredRecords",history->filtered_records()},{"retainedJsonBytes",history->bytes()},{"recordingDetail",static_cast<int>(history->detail())}};
    for(const auto &record:history->records())retained+=record.json;
  }
  std::vector<Entry> report{
    {"native-events.jsonl",std::move(retained)},
    {"session.json",metadata.dump(2)},
    {"summary.md","# Developer diagnostic snapshot\n\nDate: "+format_campaign_date(day)+".\n\nInvariant findings: "+std::to_string(critical)+
      ". Operational warnings: "+std::to_string(warnings)+".\n\n"+(checkpoint.empty()?"Current checkpoint unavailable: see checkpoint-error.txt.\n\n":"")+"This is a current-state inspection, not a completed automated soak. Empty logs mean no findings from these checks, not proof that every subsystem is correct.\n"},
    {"README.md","# Stellar Continuum diagnostic bundle\n\n"
      "- session.json: build, generator, seed, developer configuration, capture time and the typed-store projection census.\n"
      "- latest.dev17.json: current isolated developer checkpoint when encoding succeeds. Includes research and pending simulation state; player loaders reject it. If absent, checkpoint-error.txt explains the failure.\n"
      "- summary.md: result of current-state checks.\n"
      "- diagnostics.jsonl, errors.jsonl, warnings.jsonl: typed current invariant/operational findings; not historical AI decisions.\n"
      "- native-events.jsonl: bounded historical canonical events and newly detected findings since the current native session began. Rotation/filter counts are in session.json.\n"
      "- coverage.json: real celestial population, stable IDs and explicit forced-coverage markers.\n"
      "- performance.csv: measured phase aggregates since profiling began; zero samples mean unmeasured, not zero cost.\n"
      "- memory.json: tracked subsystem residency (used/reserved bytes and high-water marks) reported since session start; absent subsystems are unmeasured.\n"
      "- replay.json: exact-build checkpoint continuation instructions. Full command replay is not yet implemented.\n"
      "- session.log and system.txt: bounded recent native session messages and renderer/window context.\n\n"
      "Export does not advance, reveal, fund or repair the campaign. No unrelated files are collected.\n"},
    {"errors.jsonl",std::move(errors)},{"warnings.jsonl",std::move(warning_log)},{"diagnostics.jsonl",std::move(diagnostics)},
    {"coverage.json",coverage_json(world)},{"performance.csv",performance_csv(frame)},{"replay.json",replay.dump(2)},
    {"memory.json",stellar::engine::MemoryTracker::instance().export_json()}
  };
  if(checkpoint.empty())report.push_back({"checkpoint-error.txt",std::move(checkpoint_error)});
  else report.push_back({"latest.dev17.json",std::move(checkpoint)});
  return report;
}
// This explicit list belongs to the app, not the engine ZIP writer. No arbitrary
// directory traversal, user documents or prior export archives are collected.
inline std::filesystem::path export_qa_directory(const std::filesystem::path &root,std::int64_t seed,std::string_view timestamp){
  std::vector<Entry> entries;std::uint64_t bytes{};
  const auto add=[&](std::string name,bool required){
    const auto path=root/name;
    if(!std::filesystem::exists(path)){if(required)throw std::runtime_error("Missing required QA artifact: "+name);return;}
    const auto remaining=256u*1024u*1024u-bytes;
    auto snapshot=stellar::engine::read_diagnostic_file(path,static_cast<std::size_t>(std::min<std::uint64_t>(64u*1024u*1024u,remaining)));
    bytes+=snapshot.size();entries.push_back({std::move(name),std::move(snapshot)});
  };
  for(const char *name:{"session.json","summary.md","summary.json","README.md","commands.json","celestial-index.json","performance.csv"})add(name,true);
  for(const char *name:{"celestial-coverage.txt","latest.dev17.json","initial.dev17.json","critical.dev17.json","checkpoint-difference.json"})add(name,false);
  for(int slot=0;slot<3;++slot)add("periodic-"+std::to_string(slot)+".dev17.json",false);
  for(int slot=0;slot<8;++slot)add("logs/events-00"+std::to_string(slot)+".jsonl",false);
  return stellar::engine::write_diagnostic_bundle(root/"exports",archive_name(timestamp,seed),entries);
}
}
