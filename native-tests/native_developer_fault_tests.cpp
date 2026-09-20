#include "native_developer_fault_capture.hpp"
#include "developer_diagnostic_report.hpp"
#include "diagnostic_zip_test_reader.hpp"
#include <stellar/core/persistable_fresh_campaign.hpp>
#include <atomic>
#include <chrono>
#include <iostream>
#include <limits>
#include <thread>

using namespace stellar::core;
using namespace stellar::app_diagnostics;
using namespace stellar::native_developer;
using namespace stellar::native_support;
namespace fs=std::filesystem;
void check(bool b,const char *m){if(!b)throw std::runtime_error(m);}
void complete(NativeDeveloperFaultCapture &capture){
  const auto deadline=std::chrono::steady_clock::now()+std::chrono::seconds(10);
  while(capture.busy()&&std::chrono::steady_clock::now()<deadline){capture.poll();std::this_thread::yield();}
  check(!capture.busy(),"Critical writer did not finish.");
}
int main(int argc,char **argv)try{
  check(argc==4,"Expected catalog, research and scratch paths.");
  const auto catalog=load_nearby_catalog(argv[1]);
  const auto make=[&](bool developer){
    auto world=seed_persistable_fresh_campaign(9142050,catalog,
        {"2050-03-21T00:00:00Z",250,3,0,"terran_baseline",StellarPopulationOptions{},developer});
    CampaignFrame f(IntegratedAdaptiveCampaignRuntime::create_fresh(load_adaptive_research_strategic_runtime(argv[2]),std::move(world)),{},developer?CampaignFramePolicy::Developer:CampaignFramePolicy::Player);
    f.clock().set_speed(StrategicSpeed::Normal);return f;
  };
  const std::string stamp="2026-09-17T12:34:56Z";
  auto frame=make(true);CampaignDiagnosticMonitor monitor;
  monitor.history().set_detail(stellar::engine::DiagnosticDetail::ErrorsOnly);
  monitor.observe(frame,{},stamp);NativeDeveloperFaultCapture capture;
  int captures=0;
  const auto root=fs::path(argv[3])/std::to_string(std::chrono::steady_clock::now().time_since_epoch().count());
  const auto request=[&]{
    ++captures;SupportBundleRequest r;r.user_root=root;
    r.archive_name=archive_name(stamp,9142050);
    r.additional_entries=capture_developer_report(frame,{"test","test","test","test"},stamp,&monitor.history());return r;
  };
  check(!capture.observe(frame,monitor,request)&&captures==0,"Healthy campaign triggered an automatic fault export.");
  // Actual canonical invalid value, detected by the shared Core invariants.
  frame.runtime().world().campaign().economies.front().credits=std::numeric_limits<double>::quiet_NaN();
  monitor.reset();monitor.observe(frame,{},stamp);
  check(monitor.first_critical().has_value(),"Critical detection was lost at ErrorsOnly recording level.");
  const auto trigger=monitor.first_critical()->event_type;
  check(capture.observe(frame,monitor,request)&&captures==1&&frame.clock().speed()==StrategicSpeed::Paused,"Fault did not pause and capture once.");
  check(!capture.observe(frame,monitor,request)&&captures==1,"Same fault repeatedly created reports.");
  check(std::isnan(frame.runtime().world().campaign().economies.front().credits)&&frame.clock().simulation_days()==0.,"Fault handling repaired or advanced campaign state.");
  complete(capture);
  check(capture.error().empty()&&fs::is_regular_file(capture.result()),"Actual critical ZIP was not saved.");
  const auto files=diagnostic_zip_test::unzip(capture.result());
  check(files.contains("critical-trigger.jsonl")&&Json::parse(files.at("critical-trigger.jsonl"))["eventType"]==trigger,"Critical trigger missing from archive.");
  check(!files.at("errors.jsonl").empty()&&!files.contains("campaign.player17.json"),"Critical findings missing or normal save included.");
  const bool checkpoint=Json::parse(files.at("session.json"))["checkpointIncluded"];
  check(checkpoint==files.contains("latest.dev17.json")&&(!checkpoint?files.contains("checkpoint-error.txt"):true),"Partial capture status misrepresents checkpoint availability.");
  // Filtering/rotating the viewer cannot remove the first-fault latch.
  monitor.history().clear();check(monitor.first_critical()->event_type==trigger,"Viewer clear discarded the fault trigger.");
  auto normal=make(false);normal.runtime().world().campaign().economies.front().credits=-1.;
  NativeDeveloperFaultCapture player_guard;
  check(!player_guard.observe(normal,monitor,request)&&normal.clock().speed()==StrategicSpeed::Normal,"Developer response affected an ordinary campaign.");

  // A writer/capture failure stays visible and paused; no false success path.
  NativeDeveloperFaultCapture failed([](const SupportBundleRequest&)->fs::path{throw std::runtime_error("disk unavailable");});
  frame.clock().set_speed(StrategicSpeed::Normal);
  check(failed.observe(frame,monitor,[]{return SupportBundleRequest{};}),"Failure fixture not captured.");complete(failed);
  check(failed.result().empty()&&failed.error()=="disk unavailable"&&frame.clock().speed()==StrategicSpeed::Paused,"Write failure was hidden or resumed simulation.");
  failed.reset();check(!failed.latched()&&failed.error().empty(),"Campaign reset leaked failure state.");
  check(failed.observe(frame,monitor,[]()->SupportBundleRequest{throw std::runtime_error("capture unavailable");})&&failed.error()=="capture unavailable"&&!failed.busy(),"Capture failure did not stay explicit.");

  // Switch campaigns while IO is in flight: immutable prior captures survive,
  // one waiting request is retained, and saturation never overwrites evidence.
  std::promise<void> release;auto gate=release.get_future().share();std::atomic<int> writes{};
  std::vector<std::string> written;
  NativeDeveloperFaultCapture queued([&](const SupportBundleRequest &r){
    ++writes;gate.wait();written.push_back(r.system_info);return fs::path(r.system_info+".zip");
  });
  const auto named=[](std::string name){SupportBundleRequest r;r.archive_name="test.zip";r.system_info=std::move(name);return r;};
  const bool first=queued.observe(frame,monitor,[&]{return named("first");});
  queued.reset();const bool second=queued.observe(frame,monitor,[&]{return named("second");});
  queued.reset();const bool third=queued.observe(frame,monitor,[&]{return named("third");});
  const bool saturated=!queued.error().empty();
  // Release before assertions so a test failure cannot strand the async writer.
  release.set_value();complete(queued);
  check(first&&second&&third&&saturated&&writes==2&&written==std::vector<std::string>{"first","second"},"Bounded pending capture lost/replaced older evidence.");
  check(queued.result().empty()&&!queued.error().empty(),"Previous campaign export was attributed to the current campaign.");
  queued.reset();check(queued.observe(frame,monitor,[&]{return named("fourth");}),"New campaign could not capture after backpressure cleared.");complete(queued);
  check(queued.result()=="fourth.zip"&&queued.error().empty(),"New campaign completion failed after reset.");
  monitor.reset();check(!monitor.first_critical(),"New campaign retained previous critical trigger.");
  std::cout<<"Native critical pause, immutable export, failure and backpressure checks passed\n";return 0;
}catch(const std::exception &e){std::cerr<<e.what()<<'\n';return 1;}
