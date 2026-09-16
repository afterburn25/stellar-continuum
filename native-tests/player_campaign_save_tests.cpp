#include <stellar/core/player_campaign_save.hpp>
#include <stellar/core/player_campaign_json.hpp>
#include <stellar/core/adaptive_research_strategic_runtime.hpp>
#include <nlohmann/json.hpp>

#include <atomic>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <future>
#include <iostream>
#include <sstream>
#include <thread>
#include <typeinfo>

namespace fs = std::filesystem;
using namespace stellar::core;
using Json = nlohmann::json;
namespace {
void check(bool condition, const char *message) {
  if (!condition) throw std::runtime_error(message);
}
class PromiseRelease final {
public:
  explicit PromiseRelease(std::promise<void> &promise) : promise_(promise) {}
  ~PromiseRelease() { release(); }
  void release() noexcept {
    if (released_) return;
    released_ = true;
    try { promise_.set_value(); } catch (...) {}
  }
private:
  std::promise<void> &promise_;
  bool released_{};
};
std::string read(const fs::path &path) {
  std::ifstream input(path, std::ios::binary);
  if (!input) throw std::runtime_error("Could not open test file: " + path.string());
  std::ostringstream text; text << input.rdbuf();
  if (input.bad()) throw std::runtime_error("Could not read test file: " + path.string());
  return text.str();
}
struct Fixture {
  fs::path research; std::string source;
  IntegratedAdaptiveCampaignRuntime runtime() const {
    auto restored = restore_player_campaign_v17_json(load_adaptive_research_strategic_runtime(research), source);
    return std::move(restored).activate();
  }
  CampaignFrame frame() const {
    StrategicClock clock; clock.restore(42.25);
    return CampaignFrame(runtime(), clock, CampaignFramePolicy::Player);
  }
};
const PlayerCampaignCaptureOptions options{42.25,"0.1.7-alpha","2044-05-06T07:08:09+00:00"};

void detached_real_io(const Fixture &fixture, const fs::path &directory) {
  auto runtime = fixture.runtime();
  const auto prepared = PreparedPlayerCampaignSave::capture(runtime, options);
  const auto bytes = encode_player_campaign_v17_json(prepared.payload());
  runtime.world().campaign().seed += 123;
  check(encode_player_campaign_v17_json(prepared.payload()) == bytes, "Prepared DTO changed with live world");
  const auto first = directory / "interop.json";
  write_prepared_player_campaign(first, prepared, false);
  check(read(first) == bytes, "Actual atomic writer changed prepared bytes");
  auto backup = first; backup += ".bak";
  const auto newer = PreparedPlayerCampaignSave::capture(runtime, options);
  const auto new_bytes = encode_player_campaign_v17_json(newer.payload());
  check(bytes != new_bytes, "Mutation did not distinguish detached state");
  write_prepared_player_campaign(first, newer, false);
  check(read(backup) == bytes && read(first) == new_bytes, "Normal backup rotation failed");
  // Repair the primary while protecting the known-good older backup.
  { std::ofstream corrupt(first, std::ios::binary | std::ios::trunc); corrupt << "broken-primary"; }
  write_prepared_player_campaign(first, newer, true);
  check(read(first) == new_bytes && read(backup) == bytes, "Repair overwrote known-good backup");
  write_prepared_player_campaign(first, prepared, false);
  check(read(first) == bytes && read(backup) == new_bytes, "Normal rotation did not resume after repair");
  bool rejected{};
  try { write_prepared_player_campaign(fs::path(L" \t "), prepared, false); }
  catch (const std::invalid_argument &) { rejected = true; }
  check(rejected, "Whitespace write path was accepted");
  runtime.world().campaign().developer_provenance = CampaignDeveloperProvenance{true};
  rejected = false;
  try { (void)PreparedPlayerCampaignSave::capture(runtime, options); }
  catch (const PlayerCampaignPersistenceOperationError &) { rejected = true; }
  check(rejected, "Developer provenance crossed Player prepared-save boundary");
  std::cout << "sourceInteropFile=" << first.string() << '\n';
}

void scheduled_ownership(const Fixture &fixture, const fs::path &directory) {
  auto frame = fixture.frame();
  std::promise<void> entered, release;
  auto ready = entered.get_future(); auto gate = release.get_future().share();
  std::atomic<int> calls{};
  std::string captured;
  PlayerCampaignSaveController saves({}, [&](const fs::path &, const PreparedPlayerCampaignSave &value, bool) {
    ++calls; entered.set_value(); gate.wait(); captured = encode_player_campaign_v17_json(value.payload());
  });
  saves.configure(directory/"scheduled.json", 1, 0, false);
  const auto result = frame.advance(0);
  const auto expected = encode_player_campaign_v17_json(PreparedPlayerCampaignSave::capture(frame.runtime(), options).payload());
  (void)saves.after_frame(frame, result, options.game_version, options.saved_at_utc);
  const bool entered_in_time = ready.wait_for(std::chrono::seconds(5)) == std::future_status::ready;
  bool replacement_rejected{};
  try { saves.configure(directory/"other.json",2,42.25,false); }
  catch (const std::logic_error &) { replacement_rejected = true; }
  // All checks while the worker is held are collected before releasing it.
  const bool tracked = saves.pending();
  const bool not_complete = !saves.complete(50,saves.path(),1).has_value();
  (void)saves.after_frame(frame,result,options.game_version,options.saved_at_utc);
  frame.runtime().world().campaign().seed += 1;
  release.set_value();
  const auto completion = saves.complete(55,saves.path(),1,true);
  check(entered_in_time && tracked && not_complete, "Pending job ownership was lost");
  check(replacement_rejected && calls==1, "A second write/replacement entered while pending");
  check(completion && completion->succeeded && !saves.pending(), "Completed save not consumed");
  check(captured==expected, "Background job borrowed mutable live state");
  check(completion->captured_day==42.25 && saves.next_due_day()==85, "Success was scheduled from capture day");
}

void failure_retry_and_identity(const Fixture &fixture, const fs::path &directory) {
  auto frame = fixture.frame(); std::atomic<int> calls{};
  PlayerCampaignSaveController saves({}, [&](const fs::path &, const PreparedPlayerCampaignSave &, bool preserve) {
    check(preserve,"Recovery flag was cleared before successful repair");
    if (++calls==1) throw std::runtime_error("injected durable write failure");
  });
  saves.configure(directory/"retry.json",7,0,true);
  check(saves.next_due_day()==30,"Recovered campaign changed the source normal interval");
  const auto result=frame.advance(0);
  (void)saves.after_frame(frame,result,options.game_version,options.saved_at_utc);
  const auto failed=saves.complete(50,saves.path(),7,true);
  check(failed && !failed->succeeded && failed->error_message=="injected durable write failure", "Write failure diagnostics lost");
  check(saves.next_due_day()==51 && saves.preserves_recovered_backup(),"Failure lost backup flag/completion-day retry");
  // Late completion from a stale session is failure even when bytes were written.
  frame.clock().restore(52);
  (void)saves.after_frame(frame,frame.advance(0),options.game_version,options.saved_at_utc);
  const auto stale=saves.complete(60,saves.path(),8,true);
  check(stale && !stale->succeeded && saves.next_due_day()==61 && saves.preserves_recovered_backup(),"Stale completion counted as repair success");
  const auto repaired=saves.save_manual(frame.runtime(),{62,options.game_version,options.saved_at_utc});
  check(repaired.succeeded && repaired.preserved_backup && !saves.preserves_recovered_backup() && saves.next_due_day()==92,"Manual repair did not clear recovery after success");
  check(calls==3,"Unexpected hidden retry");
}

void frame_admission_and_drain(const Fixture &fixture, const fs::path &directory) {
  auto frame=fixture.frame(); std::atomic<int> calls{};
  PlayerCampaignSaveController saves({},[&](const fs::path &,const PreparedPlayerCampaignSave &,bool){++calls;});
  saves.configure(directory/"admission.json",1,0,false);
  CampaignFrameResult tactical; tactical.route=CampaignFrameRoute::Tactical; tactical.ready_for_save_capture=true;
  (void)saves.after_frame(frame,tactical,options.game_version,options.saved_at_utc);
  CampaignFrameResult unfinished; unfinished.route=CampaignFrameRoute::Strategic;
  (void)saves.after_frame(frame,unfinished,options.game_version,options.saved_at_utc);
  check(!saves.pending() && calls==0,"Tactical/unfinished frame entered scheduled persistence");
  frame.clock().set_speed(StrategicSpeed::Paused);
  (void)saves.after_frame(frame,frame.advance(0),options.game_version,options.saved_at_utc);
  bool undrained_rejected{};
  try { (void)saves.save_manual(frame.runtime(),options); }
  catch(const std::logic_error &) { undrained_rejected=true; }
  const auto drained=saves.complete(42.25,saves.path(),1,true);
  const auto manual=saves.save_manual(frame.runtime(),options);
  check(undrained_rejected && drained && drained->succeeded && manual.succeeded && !saves.pending() && calls==2,"Manual save bypassed explicit host drain");
  bool invalid_path_rejected{};
  try { saves.configure(fs::path(L" \t "),2,42.25,false); }
  catch(const std::invalid_argument &) { invalid_path_rejected=true; }
  check(invalid_path_rejected && saves.revision()==1,"Invalid configure path changed session");
  auto wrong_thread=std::async(std::launch::async,[&]{try{saves.configure(directory/"wrong.json",2,0,false);return false;}catch(const std::logic_error &){return true;}});
  check(wrong_thread.get(),"Worker was allowed to control live save state");
}

void blocking_manual_drain(const Fixture &fixture,const fs::path &directory) {
  std::promise<void> entered, release;
  auto entered_future=entered.get_future(); auto gate=release.get_future().share();
  std::atomic<int> calls{}; std::atomic<bool> first_finished{};
  auto manual=std::async(std::launch::async,[&]{
    auto frame=fixture.frame();
    PlayerCampaignSaveController saves({},[&](const fs::path &,const PreparedPlayerCampaignSave &,bool){
      if(++calls==1){entered.set_value();gate.wait();first_finished=true;throw std::runtime_error("pending failure remains visible");}
      else check(first_finished,"Manual writer overtook a blocked scheduled writer");
    });
    saves.configure(directory/"drain.json",4,0,false);
    (void)saves.after_frame(frame,frame.advance(0),options.game_version,options.saved_at_utc);
    const auto drained=saves.complete(42.25,saves.path(),4,true);
    check(drained && !drained->succeeded && drained->error_message=="pending failure remains visible" && drained->captured_day==42.25,"Pending failure was hidden before manual retry");
    return saves.save_manual(frame.runtime(),options);
  });
  const bool started=entered_future.wait_for(std::chrono::seconds(5))==std::future_status::ready;
  const bool blocked=manual.wait_for(std::chrono::milliseconds(20))==std::future_status::timeout;
  const bool one_writer=calls==1;
  release.set_value();
  const auto saved=manual.get();
  check(started && blocked && one_writer && saved.succeeded && calls==2,"Manual save did not block/drain before its own write");
}

void async_manual_admission(const Fixture &fixture,const fs::path &directory) {
  auto runtime=fixture.runtime();
  const auto expected=encode_player_campaign_v17_json(
      PreparedPlayerCampaignSave::capture(runtime,options).payload());
  std::promise<void> entered,release;
  auto entered_future=entered.get_future();auto gate=release.get_future().share();
  PromiseRelease release_guard(release);
  std::atomic<int> calls{};std::string captured;
  PlayerCampaignSaveController saves({},[&](const fs::path &,const PreparedPlayerCampaignSave &prepared,bool){
    ++calls;entered.set_value();gate.wait();captured=encode_player_campaign_v17_json(prepared.payload());
  });
  saves.configure(directory/"async-manual.json",9,0,false);
  const auto admitted=saves.begin_manual(runtime,options);
  const auto entered_in_time=entered_future.wait_for(std::chrono::seconds(5))==std::future_status::ready;
  runtime.world().campaign().seed+=1;
  bool replacement_rejected{},second_rejected{};
  try{saves.configure(directory/"replacement.json",10,42.25,false);}catch(const std::logic_error&){replacement_rejected=true;}
  try{(void)saves.begin_manual(runtime,options);}catch(const std::logic_error&){second_rejected=true;}
  const bool no_completion=!saves.complete(50,saves.path(),9).has_value();
  release_guard.release();
  const auto complete=saves.complete(51,saves.path(),9,true);
  check(!admitted && entered_in_time && saves.pending()==false && no_completion,
        "Async manual admission did not return while its writer was blocked");
  check(replacement_rejected&&second_rejected&&calls==1&&complete&&complete->succeeded,
        "Async manual admission allowed a replacement or parallel writer");
  check(captured==expected,"Async manual writer borrowed mutable live state");
}

void async_manual_failure_and_owner(const Fixture &fixture,const fs::path &directory) {
  auto runtime=fixture.runtime();std::atomic<int> calls{};
  PlayerCampaignSaveController saves({},[&](const fs::path &,const PreparedPlayerCampaignSave &,bool preserve){
    check(preserve,"Async failure lost the recovery backup flag before completion");
    if(++calls==1)throw std::runtime_error("async durable write failure");
  });
  saves.configure(directory/"async-retry.json",11,0,true);
  const auto admitted=saves.begin_manual(runtime,options);
  const auto failed=saves.complete(50,saves.path(),11,true);
  check(!admitted&&failed&&!failed->succeeded&&failed->preserved_backup&&
        failed->error_message=="async durable write failure"&&saves.next_due_day()==51&&
        saves.preserves_recovered_backup(),"Async manual failure lost its retry or recovery state");
  const auto retry=saves.begin_manual(runtime,options);
  const auto repaired=saves.complete(51,saves.path(),11,true);
  check(retry==std::nullopt&&repaired&&repaired->succeeded&&saves.next_due_day()==81&&
        !saves.preserves_recovered_backup(),"Async manual failure/retry scheduling or recovery state is wrong");
  runtime.world().campaign().developer_provenance=CampaignDeveloperProvenance{true};
  const auto capture_failed=saves.begin_manual(runtime,options);
  check(capture_failed&&!capture_failed->succeeded&&capture_failed->error_message.size()>0&&!saves.pending(),
        "Async manual capture failure was not returned synchronously");
  auto wrong_thread=std::async(std::launch::async,[&]{try{
    (void)saves.begin_manual(runtime,options);return false;
  }catch(const std::logic_error&){return true;}catch(...){return false;}});
  check(wrong_thread.get(),"Wrong-thread async admission reached campaign capture");
}
} // namespace
int main(int argc,char **argv) try {
  if(argc!=4) throw std::invalid_argument("Usage: player_campaign_save_tests <Player17 JSON fixture> <research root> <exclusive output parent>");
  const auto fixture_json=Json::parse(read(fs::absolute(argv[1])));
  std::string source;
  for(const auto &row:fixture_json.at("Rows")) if(row.at("Name")=="valid-current17") source=row.at("InputJson").get<std::string>();
  check(!source.empty(),"Missing source baseline");
  const Fixture fixture{fs::absolute(argv[2]),source};
  const auto parent=fs::absolute(argv[3]);fs::create_directories(parent);
  const auto directory=parent/("save-replay-"+std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
  check(fs::create_directory(directory),"Cannot claim exclusive test directory");
  detached_real_io(fixture,directory);
  scheduled_ownership(fixture,directory);
  failure_retry_and_identity(fixture,directory);
  frame_admission_and_drain(fixture,directory);
  blocking_manual_drain(fixture,directory);
  async_manual_admission(fixture,directory);
  async_manual_failure_and_owner(fixture,directory);
  std::cout<<"Player17 prepared/async save: detached state, real atomic IO, backup repair, one writer, frame admission, completion-day scheduling, stale identity, manual drain, async manual admission, provenance and thread ownership passed\n";
  return 0;
}catch(const std::exception &error){std::cerr<<"ExceptionType: "<<typeid(error).name()<<"\nMessage: "<<error.what()<<"\nCurrentDirectory: "<<fs::current_path().string()<<'\n';return 1;}
