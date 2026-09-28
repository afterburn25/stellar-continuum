#include "developer_qa_host.hpp"
#include "developer_diagnostic_report.hpp"
#include "diagnostic_zip_test_reader.hpp"
#include <filesystem>
#include <fstream>
#include <iostream>
#include <chrono>
#include <nlohmann/json.hpp>
namespace fs=std::filesystem;
using Json=nlohmann::json;
void check(bool value,const char *message){if(!value)throw std::runtime_error(message);}
Json json(const fs::path &path){std::ifstream in(path);check(bool(in),"Missing QA artifact.");return Json::parse(in);}
int invoke(const std::string &exe,const fs::path &assets,const fs::path &output,std::vector<std::string> extra,bool entitlement=true){
  std::vector<std::string> values{exe,"--developer-qa","--headless","--asset-root",assets.string(),"--output",output.string()};
  if(entitlement)values.push_back("--devtools");values.insert(values.end(),extra.begin(),extra.end());
  std::vector<char*> argv;for(auto &s:values)argv.push_back(s.data());return run_developer_qa(static_cast<int>(argv.size()),argv.data());
}
int main(int argc,char **argv)try{
  check(argc==3,"Expected asset/output paths.");
  const fs::path assets=argv[1],root=fs::path(argv[2])/std::to_string(std::chrono::steady_clock::now().time_since_epoch().count());
  const auto denied=[&](std::vector<std::string> options,bool entitlement){try{invoke(argv[0],assets,root/"denied",std::move(options),entitlement);}catch(const std::exception&){return true;}return false;};
  check(denied({"--ticks","8"},false)&&!fs::exists(root/"denied"),"Unentitled QA created files.");
  check(denied({"--speed","3"},true),"Invalid speed accepted.");
  check(denied({"--ticks","8","--years","1"},true),"Conflicting run lengths accepted.");
  check(invoke(argv[0],assets,root/"fast",{"--ticks","80","--systems","250","--civilizations","3","--ancients","0","--ai-control","--checkpoint-days","1"})==0,"Real QA run failed.");
  fs::path bundle;
  for(const auto &entry:fs::recursive_directory_iterator(root/"fast/exports"))if(entry.path().extension()==".zip")bundle=entry.path();
  check(!bundle.empty(),"QA omitted its diagnostic ZIP.");
  const auto exported=diagnostic_zip_test::unzip(bundle);
  for(const auto &name:{"latest.dev17.json","initial.dev17.json","summary.json","performance.csv","logs/events-000.jsonl"})
    check(exported.at(name)==diagnostic_zip_test::read(root/"fast"/name),"QA ZIP omitted/changed an authoritative artifact.");
  check(!exported.contains("campaign.player17.json"),"QA bundle included a player save.");
  // A failed run can lack an initial/latest save but its surviving reports
  // must still be exportable. Unrelated files never enter the archive.
  const auto failed=root/"failed-report";fs::create_directories(failed);
  for(const auto &name:{"session.json","summary.md","summary.json","README.md","commands.json","celestial-index.json","performance.csv"})
    diagnostic_zip_test::write(failed/name,"evidence");
  diagnostic_zip_test::write(failed/"unrelated-private.txt","exclude");
  diagnostic_zip_test::write(failed/"critical.dev17.json","critical checkpoint");
  const auto failed_zip=diagnostic_zip_test::unzip(stellar::app_diagnostics::export_qa_directory(failed,42,"2026-09-17"));
  check(failed_zip.at("critical.dev17.json")=="critical checkpoint"&&!failed_zip.contains("unrelated-private.txt"),"Failure archive omitted critical evidence or crawled unrelated files.");
  const auto summary=json(root/"fast/summary.json");
  check(summary["passed"]==true&&summary["completedTicks"]==80&&summary["endDay"]==20&&summary["finalCheckpointRoundTrip"]==true,"QA duration/checkpoint result incorrect.");
  check(summary["profiledTicks"]==80&&summary["invariantChecks"].get<int>()>=20,"Tick timing or daily validation missing.");
  check(summary["operationalFindings"].is_array(),"Operational findings omitted from QA summary.");
  for(const auto &phase:summary["phases"].items())check(phase.value()["samples"]==80,"Authoritative phase profiling missed a tick.");
  check(summary["peakPrivateMemoryBytes"].get<std::uint64_t>()>=summary["finalPrivateMemoryBytes"].get<std::uint64_t>(),"Peak memory excluded final measurement.");
  const auto metadata=json(root/"fast/session.json");check(metadata["executableSha256"].get<std::string>().size()==64,"Exact build digest missing.");
  check(metadata["generatorVersion"]=="stellar-population-v1","QA bypassed current population generator.");
  check(metadata["forcedCelestialCoverage"]==false,"Natural run forced celestial categories.");
  const auto natural_index=json(root/"fast/celestial-index.json");
  for(const auto &count:natural_index["counts"])check(count["forced"]==0,"Natural index marked injected objects.");
  check(fs::exists(root/"fast/README.md")&&fs::exists(root/"fast/celestial-coverage.txt"),"Bundle explanation/coverage report missing.");
  int periodic=0;for(const auto &f:fs::directory_iterator(root/"fast"))if(f.path().filename().string().starts_with("periodic-")&&f.path().extension()==".json")++periodic;
  check(periodic==3,"Periodic checkpoints grew beyond rotating slots.");
  check(invoke(argv[0],assets,root/"resumed",{"--load",(root/"fast/latest.dev17.json").string(),"--ticks","7","--speed","1"})==0,"QA could not resume checkpoint.");
  auto resumed=json(root/"resumed/summary.json");check(resumed["startDay"]==20&&resumed["endDay"]==21.75&&resumed["completedTicks"]==7,"Partial final batch overshot requested time.");
  bool existing=false;try{invoke(argv[0],assets,root/"fast",{"--ticks","1"});}catch(const std::exception&){existing=true;}
  check(existing&&json(root/"fast/summary.json")==summary,"Existing diagnostic session was overwritten.");
  // Identical initial checkpoint: profiling and acceleration must not change
  // canonical outcomes. Timestamps are explicitly presentation-only metadata.
  check(invoke(argv[0],assets,root/"slow",{"--load",(root/"fast/initial.dev17.json").string(),"--ticks","80","--speed","1"})==0,"Matched slow continuation failed.");
  auto fast=json(root/"fast/latest.dev17.json"),slow=json(root/"slow/latest.dev17.json");
  fast["Simulation"]["Speed"]=slow["Simulation"]["Speed"];
  fast["Campaign"]["SavedAtUtc"]=slow["Campaign"]["SavedAtUtc"];
  if(fast!=slow)std::cerr<<Json::diff(fast,slow).dump().substr(0,3000)<<'\n';
  check(fast==slow,"Headless acceleration/checkpoint continuation diverged.");
  // Successful runs carry no diagnostic value; keep run directories only on
  // failure so repeated suites do not exhaust the drive (~0.6 GB per run).
  {std::error_code cleanup;fs::remove_all(root,cleanup);}
  std::cout<<"developer QA host tests passed\n";return 0;
}catch(const std::exception&e){std::cerr<<e.what()<<'\n';return 1;}
