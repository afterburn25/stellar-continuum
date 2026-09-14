#include "campaign_frame_test_support.cpp"
#include <stellar/core/campaign_frame.hpp>
#include <algorithm>
#include <filesystem>
#include <iostream>
#include <span>
using namespace stellar::core;
namespace fs = std::filesystem;
namespace gate097 {
const Json &named(const Json &rows, std::string_view name) {
  const auto found = std::find_if(rows.begin(), rows.end(), [&](const Json &row) { return row.at("Name").get<std::string>() == name; });
  require(found != rows.end(), "missing row " + std::string(name)); return *found;
}
bool equal_row_json(const Json &left,const Json &right,std::string &path){
  if(left.is_number()&&right.is_number()){
    if(left.get<double>()==0.&&right.get<double>()==0.)return true;
    const bool li=left.is_number_integer()||left.is_number_unsigned(),ri=right.is_number_integer()||right.is_number_unsigned();
    if(li&&ri)return left.get<std::int64_t>()==right.get<std::int64_t>();
    const double a=left.get<double>(),b=right.get<double>();
    if(path.find("/TacticalLoadout/")!=std::string::npos||path.find("/TacticalVessel/")!=std::string::npos||path.find("/Formations/")!=std::string::npos)
      return std::bit_cast<std::uint32_t>(static_cast<float>(a))==std::bit_cast<std::uint32_t>(static_cast<float>(b));
    return (std::isnan(a)&&std::isnan(b))||std::abs(a-b)<=1e-10;
  }
  if(left.type()!=right.type()||left.size()!=right.size())return false;
  if(left.is_object())for(const auto &[key,value]:left.items()){if(!right.contains(key)){path+="/"+key;return false;}const auto prior=path;path+="/"+key;if(!equal_row_json(value,right.at(key),path))return false;path=prior;}
  else if(left.is_array())for(std::size_t i=0;i<left.size();++i){const auto prior=path;path+="/"+std::to_string(i);if(!equal_row_json(left[i],right[i],path))return false;path=prior;}
  else if(left!=right)return false;
  return true;
}
void row_equal(const Json &actual,const Json &expected,const std::string &context){std::string path;if(equal_row_json(actual,expected,path))return;Json a=actual,e=expected;try{const Json::json_pointer pointer(path);a=actual.at(pointer);e=expected.at(pointer);}catch(const std::exception&){}throw std::runtime_error(context+" differed at "+path+" actual="+a.dump()+" expected="+e.dump());}
void verify_fixture(const Json &fixture, const fs::path &source) {
  require(fixture.at("SchemaVersion").get<int>() == 1 && fixture.at("RowCount").get<int>() == 14 && fixture.at("Rows").size() == 14, "campaign fixture schema/count mismatch");
  require(!fixture.at("Boundary").at("GodotMainInvoked").get<bool>(), "fixture falsely claims Godot Main invocation");
  for (const auto &item : fixture.at("SourceFiles")) { const auto path = source / item.at("Path").get<std::string>(); const auto bytes = read_file(path); const auto digest = detail::adaptive_research_sha256(std::span(reinterpret_cast<const std::uint8_t *>(bytes.data()), bytes.size())); require(hex(digest) == item.at("Sha256").get<std::string>(), "source fingerprint mismatch: " + path.string()); }
}
Json clock_json(const StrategicClock &clock) { return {{"Speed", static_cast<int>(clock.speed())}, {"ResumeSpeed", static_cast<int>(clock.resume_speed())}, {"SimulationDays", clock.simulation_days()}, {"EffectiveMultiplier", clock.effective_multiplier()}, {"RequestedMultiplier", clock.requested_multiplier()}, {"BacklogDays", clock.backlog_days()}}; }
Json campaign_json(const IntegratedAdaptiveCampaignRuntime &runtime) { return {{"World", encode_world(runtime.world().campaign())}, {"Research", encode_research_snapshot(runtime.research_runtime(), runtime.research())}, {"Diplomacy", diplomacy_support::jsnapshot(runtime.diplomacy().snapshot())}}; }
Json encounter_json(const std::optional<CampaignMassiveEncounter> &value) { if (!value) return nullptr; Json formations=Json::array(); for(const auto &f:value->battle.formations) formations.push_back({{"Id",f.id},{"CivilizationId",f.civilization_id},{"FleetId",f.fleet_id},{"ShieldPool",f.shield_pool},{"ArmorPool",f.armor_pool},{"HullPool",f.hull_pool},{"DestroyedShips",f.destroyed_ships},{"Escaped",f.escaped},{"Surrendered",f.surrendered}}); Json vessels=Json::array(); for(const auto &v:value->vessels)vessels.push_back({{"FleetId",v.fleet_id},{"FormationId",v.formation_id}}); return {{"SystemId",value->system_id},{"StartedDay",value->started_day},{"Reconciled",value->reconciled},{"Battle",{{"Tick",value->battle.tick},{"SimulatedSeconds",value->battle.simulated_seconds},{"PendingSeconds",value->battle.pending_seconds},{"Formations",formations}}},{"Vessels",vessels}}; }
Json state_json(CampaignFrame &frame) { const auto &world = frame.runtime().world().campaign(); return {{"Clock", clock_json(frame.clock())}, {"TacticalSpeed", frame.tactical_clock().speed_multiplier()}, {"TacticalResumeSpeed", frame.tactical_resume_speed()}, {"Active", world.active_combat_encounter && !world.active_combat_encounter->reconciled}, {"Encounter",encounter_json(world.active_combat_encounter)}, {"Campaign", campaign_json(frame.runtime())}}; }
Json frame_json(const CampaignFrameResult &result) { return {{"Route", result.route == CampaignFrameRoute::Tactical ? "Tactical" : "Strategic"}, {"Steps", result.completed_substeps}, {"EndDays", result.completed_end_days}, {"TacticalAccepted", result.tactical_accepted_seconds}, {"TacticalEvents", result.tactical_events.size()}, {"TacticalCompleted", result.tactical_completed}, {"ReadyForSaveCapture", result.ready_for_save_capture}}; }
IntegratedAdaptiveCampaignRuntime restore_owner(const fs::path &research_root, const Json &state) {
  auto runtime = load_adaptive_research_strategic_runtime(research_root); auto world = decode_world(state.at("Campaign").at("World"));
  auto research = research_support::decode_campaign_snapshot(camelize(state.at("Campaign").at("Research")));
  auto diplomacy = DiplomacyState::restore(diplomacy_support::snapshot(state.at("Campaign").at("Diplomacy")));
  return IntegratedAdaptiveCampaignRuntime::restore_research(std::move(runtime), std::move(world), research, std::move(diplomacy), state.at("Clock").at("SimulationDays").get<double>());
}
StrategicClock restore_clock(const Json &state) { StrategicClock clock; const auto &snapshot = state.at("Clock"); clock.set_speed(static_cast<StrategicSpeed>(snapshot.at("ResumeSpeed").get<int>())); clock.set_speed(static_cast<StrategicSpeed>(snapshot.at("Speed").get<int>())); clock.restore(snapshot.at("SimulationDays").get<double>()); return clock; }
CampaignFrame restore_frame(const fs::path &research_root, const Json &row) {
  const auto &before = row.at("Before"); const auto policy = row.at("Policy").get<std::string>() == "Developer" ? CampaignFramePolicy::Developer : CampaignFramePolicy::Player;
  auto owner=restore_owner(research_root,before); const auto name=row.at("Name").get<std::string>(); if(before.at("Active").get<bool>()){CampaignMassiveCombat combat([](int a,int b){return a!=b;});const auto started=combat.begin(owner.world().campaign(),1,1,before.at("Clock").at("SimulationDays"));require(started.accepted,name+": could not reconstruct encounter");if(name=="completed-battle-restores-speed-consumes-frame"){auto &formations=owner.world().campaign().active_combat_encounter->battle.formations;auto enemy=std::find_if(formations.begin(),formations.end(),[](const auto &f){return f.civilization_id==2;});require(enemy!=formations.end(),name+": enemy missing");enemy->surrendered=true;}}
  CampaignFrame frame(std::move(owner), restore_clock(before), policy); frame.set_tactical_resume_speed(before.at("TacticalResumeSpeed").get<double>()); if(name=="menu-resume-restores-speed"){frame.set_tactical_speed(2);frame.pause_tactical_for_menu();}else frame.set_tactical_speed(before.at("TacticalSpeed").get<double>()); return frame;
}
Json execute_source_row(const fs::path &research_root, const Json &row, Json &after) {
  auto frame = restore_frame(research_root, row); const auto name = row.at("Name").get<std::string>(); row_equal(state_json(frame), row.at("Before"), name + ": before"); const double delta = row.at("Input").value("Delta", 0.0); Json result;
  if (name == "battle-takes-strategic-pause") { const auto order = frame.begin_tactical(1); result = {{"Order", {{"Accepted", order.accepted}, {"Message", order.message}}}, {"Frame", frame_json(frame.advance(delta))}}; }
  else if (name == "menu-pause-zero-admission") { frame.pause_tactical_for_menu(); frame.set_menu_open(true); result = frame_json(frame.advance(delta)); }
  else if (name == "menu-resume-restores-speed") { frame.set_menu_open(false); frame.resume_tactical_after_menu(); result = frame_json(frame.advance(delta)); }
  else if (name == "begin-existing-battle-rejected") { const auto order = frame.begin_tactical(row.at("Input").at("FleetId")); result = {{"Accepted", order.accepted}, {"Message", order.message}}; }
  else result = frame_json(frame.advance(delta)); after = state_json(frame); return result;
}
void execute_contract_row(const fs::path &research_root, const Json &integrated, const Json &row, Json &after, Json &result) {
  const auto name = row.at("Name").get<std::string>(); after = row.at("After");
  if (name == "move-owner-retains-borrowed-diplomacy") { CampaignFrame first(create_owner_for_row(research_root, named(integrated.at("Rows"), "fresh-zero")), StrategicClock{}, CampaignFramePolicy::Player); CampaignFrame moved(std::move(first)); CampaignFrame assigned(create_owner_for_row(research_root, named(integrated.at("Rows"), "fresh-zero")), StrategicClock{}, CampaignFramePolicy::Player); assigned = std::move(moved); require(assigned.advance(0).strategic_results.size() == 1, "moved owner lost runtime borrowers"); result = {{"StableOwnerRequired", true}, {"CanonicalWorldCount", 1}}; }
  else { CampaignFrame frame(create_owner_for_row(research_root, named(integrated.at("Rows"), row.at("IntegratedRow").get<std::string>())), StrategicClock{}, CampaignFramePolicy::Player); std::size_t completed = 0; bool ready = false, failed = false; try { const auto value = frame.advance(row.at("Input").at("Delta")); completed = value.strategic_results.size(); ready = value.ready_for_save_capture; } catch (const std::exception &) { failed = true; } require(failed, "failed source-integrated step returned a completed frame"); result = {{"ReadyForSaveCapture", ready}, {"CompletedStrategicResults", completed}}; }
}
void replay_rows(const fs::path &research_root, const Json &fixture, const Json &integrated) {
  std::size_t source_rows = 0, contract_rows = 0; for (const auto &row : fixture.at("Rows")) { Json after, result; const auto route = row.at("Route").get<std::string>(); if (route == "Ownership" || route == "StrategicFailure") { execute_contract_row(research_root, integrated, row, after, result); ++contract_rows; } else { result = execute_source_row(research_root, row, after); ++source_rows; } const auto name = row.at("Name").get<std::string>(); row_equal(result, row.at("Result"), name + ": result"); row_equal(after, row.at("After"), name + ": after"); } require(source_rows == 12 && contract_rows == 2, "unexpected source/contract row partition");
}
void run(const fs::path &research_root, const fs::path &source_root, const fs::path &fixture_path, const fs::path &integrated_path) { const auto fixture_bytes = read_file(fixture_path), integrated_bytes = read_file(integrated_path); const auto fixture = Json::parse(fixture_bytes), integrated = Json::parse(integrated_bytes); verify_fixture(fixture, source_root / "src/Game"); replay_rows(research_root, fixture, integrated); require(read_file(fixture_path) == fixture_bytes && read_file(integrated_path) == integrated_bytes, "fixture changed during replay"); std::cout << "campaign frame parity: 12/12 source rows and 2/2 native contract rows passed\n"; }
}
int main(int argc, char **argv) { try { if (argc != 5) throw std::invalid_argument("Usage: campaign_frame_tests <research-root> <source-root> <fixture> <integrated-fixture>"); gate097::run(fs::absolute(argv[1]), fs::absolute(argv[2]), fs::absolute(argv[3]), fs::absolute(argv[4])); return 0; } catch (const std::exception &error) { std::cerr << "ExceptionType: " << typeid(error).name() << "\nMessage: " << error.what() << "\nCurrentDirectory: " << fs::current_path().string() << '\n'; return 1; } }
