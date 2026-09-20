#include <stellar/core/campaign_frame.hpp>
#include <stellar/core/developer_campaign.hpp>
#include <stellar/core/player_campaign_json.hpp>
#include <stellar/core/galaxy_catalog.hpp>
#include <stellar/core/persistable_fresh_campaign.hpp>
#include <stellar/engine/foundation.hpp>
#include <stellar/core/diplomacy_simulation.hpp>
#include <filesystem>
#include <nlohmann/json.hpp>
#include <iostream>
#include <stdexcept>

using namespace stellar::core;
void require(bool condition,const char *message){if(!condition)throw std::runtime_error(message);}

int main(int argc,char **argv)try{
  require(argc==3,"Expected research root and catalog.");
  const auto root=std::filesystem::path(argv[1]);const auto catalog=load_nearby_catalog(argv[2]);
  const auto make=[&](int civilizations=1){
    auto world=seed_persistable_fresh_campaign(9142050,catalog,{"2050-03-21T00:00:00Z",250,civilizations,0,"terran_baseline"});
    // Match the canonical Sol migration performed at the load boundary before
    // comparing uninterrupted and reloaded clock continuations.
    world.bodies=upgrade_saved_sol_catalog(world.bodies,world.systems);
    for(auto& body:world.bodies)if(!body.appearance){const auto star=std::ranges::find(world.systems,body.system_id,&StellarSystem::id);
      body.appearance=planet_appearance_for_existing(0,body,star!=world.systems.end()&&star->stellar_object?&*star->stellar_object:nullptr);}
    world.developer_provenance=CampaignDeveloperProvenance{};
    return CampaignFrame(IntegratedAdaptiveCampaignRuntime::create_fresh(load_adaptive_research_strategic_runtime(root),std::move(world)),{},CampaignFramePolicy::Developer);
  };
  const auto saved=[](CampaignFrame &frame){return capture_developer_campaign_json(frame.runtime(),{frame.clock().simulation_days(),"test","2050-03-21T00:00:00Z"});};
  {
    auto one=make(),twenty_five=make();
    for(auto* frame:{&one,&twenty_five}){
      frame->clock().set_days_per_second(1./24.);
      auto& world=frame->runtime().world().campaign();
      world.systems.front().stellar_object=generate_stellar_physics(world.seed,StellarObjectType::MRedDwarf);
      initialize_stellar_activity(world.seed,world.systems);
      auto& activity=world.systems.front().stellar_activity->front();
      activity.profile.level=StellarActivityLevel::FlareStar;activity.next_event_day=.001;
      frame->runtime().stellar_activity().rebuild(world.systems);
    }
    one.set_developer_speed(1);twenty_five.set_developer_speed(25);
    for(int i=0;i<32;++i){(void)one.advance(.25);(void)twenty_five.advance(.25);}
    // Accelerated strategic ticks are intentionally bounded per frame. Draining
    // that work with no new real time must not advance stellar activity again.
    while(twenty_five.developer_ticks_behind())(void)twenty_five.advance(0.);
    const auto& one_star=one.runtime().world().campaign().systems.front();
    const auto& fast_star=twenty_five.runtime().world().campaign().systems.front();
    require(one_star.stellar_activity==fast_star.stellar_activity&&one_star.stellar_activity->front().counter>0,
        "Game speed changed flare frequency, identities, sites or durations");
    require(std::abs(one.runtime().stellar_activity_day()-8./24.)<1e-12&&
        one.runtime().stellar_activity_day()==twenty_five.runtime().stellar_activity_day(),"Activity clock did not retain fixed 1x cadence");
    require(twenty_five.clock().simulation_days()>one.clock().simulation_days()*20,"Flare isolation stopped strategic acceleration");
    const auto checkpoint=saved(twenty_five);
    auto restored=restore_developer_campaign_json(load_adaptive_research_strategic_runtime(root),checkpoint);
    auto loaded=std::move(restored).activate();
    require(loaded.stellar_activity_day()==twenty_five.runtime().stellar_activity_day(),"Save/reload restarted activity clock");
    (void)loaded.advance_stellar_activity(3.);(void)twenty_five.runtime().advance_stellar_activity(3.);
    require(loaded.world().campaign().systems.front().stellar_activity==fast_star.stellar_activity,
        "Restoring the unscaled activity clock changed future events");
    for(const auto& event:fast_star.stellar_activity->front().events){
      const auto& replay=loaded.world().campaign().systems.front().stellar_activity->front().events;
      const auto found=std::ranges::find(replay,event.id,&StellarEruptionEvent::id);
      require(found!=replay.end()&&stellar_eruption_sample(*found,loaded.stellar_activity_day()).fraction==
          stellar_eruption_sample(event,twenty_five.runtime().stellar_activity_day()).fraction,"Reload changed flare animation progress");
    }
    auto legacy=nlohmann::ordered_json::parse(checkpoint);legacy["Campaign"]["Galaxy"].erase("StellarActivityDay");
    auto migration=restore_developer_campaign_json(load_adaptive_research_strategic_runtime(root),legacy.dump());
    const auto epoch=migration.simulation_days();auto migrated=std::move(migration).activate();
    require(migrated.stellar_activity_day()==epoch&&migrated.world().campaign().systems.front().stellar_activity==one_star.stellar_activity,
        "Old save migration rerolled events or chose the wrong epoch");
    for(auto bad:{nlohmann::json(-1),nlohmann::json(1e13),nlohmann::json("fast"),nlohmann::json(true)}){
      auto broken=nlohmann::json::parse(checkpoint);broken["Campaign"]["Galaxy"]["StellarActivityDay"]=bad;bool rejected=false;
      try{(void)restore_developer_campaign_json(load_adaptive_research_strategic_runtime(root),broken.dump());}catch(const std::exception&){rejected=true;}
      require(rejected,"Malformed activity clock was accepted");
    }
    one.clock().set_speed(StrategicSpeed::Paused);const auto frozen=saved(one);(void)one.advance(50.);
    require(saved(one)==frozen,"Paused game advanced flare schedule");
    one.clock().resume();one.set_menu_open(true);(void)one.advance(50.);
    require(saved(one)==frozen,"Open menu advanced flare schedule");
  }
  {
    auto make_player=[&](){
      auto world=seed_persistable_fresh_campaign(9142050,catalog,{"2050-03-21T00:00:00Z",250,1,0,"terran_baseline"});
      world.systems.front().stellar_object=generate_stellar_physics(world.seed,StellarObjectType::MRedDwarf);
      initialize_stellar_activity(world.seed,world.systems);
      auto& activity=world.systems.front().stellar_activity->front();
      activity.profile.level=StellarActivityLevel::FlareStar;activity.next_event_day=.001;
      StrategicClock clock;clock.set_days_per_second(1./24.);
      return CampaignFrame(IntegratedAdaptiveCampaignRuntime::create_fresh(load_adaptive_research_strategic_runtime(root),std::move(world)),clock,CampaignFramePolicy::Player);
    };
    auto normal=make_player(),fast=make_player();fast.clock().set_speed(StrategicSpeed::Maximum);
    for(int i=0;i<32;++i){(void)normal.advance(.25);(void)fast.advance(.25);}
    require(normal.runtime().stellar_activity_day()==fast.runtime().stellar_activity_day()&&
        normal.runtime().world().campaign().systems.front().stellar_activity==fast.runtime().world().campaign().systems.front().stellar_activity,
        "Player speed changed flare timing or frequency");
    require(fast.clock().simulation_days()>normal.clock().simulation_days()*7.,"Player strategic acceleration stopped");
  }
  auto hourly=make();hourly.clock().set_days_per_second(1./24.);hourly.set_developer_speed(1);
  {
    auto eruptions=make();auto& world=eruptions.runtime().world().campaign();
    // The legacy test catalog predates physical star metadata. Supply the
    // physical Sol record that new native campaigns already generate.
    world.systems.front().stellar_object=generate_stellar_physics(world.seed,StellarObjectType::GYellowStar);
    initialize_stellar_activity(world.seed,world.systems);
    eruptions.runtime().stellar_activity().rebuild(world.systems);
    auto star=std::ranges::find_if(world.systems,[](const auto& s){return s.stellar_activity&&s.stellar_activity->front().profile.spectral==EruptionSpectralClass::G;});
    require(star!=world.systems.end(),"Save replay requires a G-class host");
    StellarActivityCommand cmd;cmd.system_id=star->id;cmd.action=StellarActivityAction::Force;cmd.type=StellarEruptionType::Superflare;cmd.longitude=1.25;cmd.variant=11;
    cmd.event_id=apply_developer_stellar_activity(world,eruptions.runtime().stellar_activity(),0,cmd);
    cmd.action=StellarActivityAction::Scrub;cmd.fraction=.65;(void)apply_developer_stellar_activity(world,eruptions.runtime().stellar_activity(),0,cmd);
    const auto expected=star->stellar_activity;
    auto eruption_save=restore_developer_campaign_json(load_adaptive_research_strategic_runtime(root),saved(eruptions));
    auto replay=std::move(eruption_save).activate();const auto& replay_world=replay.world().campaign();
    const auto restored_star=std::ranges::find(replay_world.systems,star->id,&StellarSystem::id);
    require(restored_star!=replay_world.systems.end()&&restored_star->stellar_activity==expected,"Full developer save lost paused 65-percent eruption, activity, position or variant");
  }
  (void)hourly.advance(1.);require(std::abs(hourly.clock().simulation_days()-1./24.)<1e-12,"Developer 1x did not advance one hour");
  hourly.set_developer_speed(25);(void)hourly.advance(1.);
  for(int guard=0;hourly.developer_ticks_behind()&&guard<20;++guard)(void)hourly.advance(0);
  require(std::abs(hourly.clock().simulation_days()-26./24.)<1e-12,"25x changed authoritative hourly cadence");
  auto ordinary=make();ordinary.runtime().world().campaign().developer_provenance.reset();
  bool denied=false;try{ordinary.set_developer_speed(25);}catch(const std::exception&){denied=true;}
  require(denied,"Unmarked campaign enabled QA acceleration.");
  denied=false;try{set_developer_ai_control(ordinary.runtime(),true);}catch(const std::exception&){denied=true;}
  require(denied,"Player campaign enabled developer AI takeover.");
  auto slow=make(),fast=make();slow.set_developer_speed(1);fast.set_developer_speed(25);
  for(int i=0;i<100;++i){const auto step=slow.advance(.25);require(step.completed_substeps==std::vector<double>{.25},"Ordinary fixed cadence changed.");}
  const auto first=fast.advance(1.);
  require(first.completed_substeps.size()==8&&fast.developer_ticks_behind()==92&&fast.clock().simulation_days()==2.,"Accelerated frame dropped work or exceeded its tick budget.");
  const auto pending=saved(fast);
  {
    auto damaged = nlohmann::ordered_json::parse(pending);
    damaged["Campaign"]["Galaxy"]["Systems"][0]["Id"] = "bad-native-system-id";
    const auto text = damaged.dump(2);
    bool rejected = false;
    try { (void)restore_developer_campaign_json(load_adaptive_research_strategic_runtime(root), text); }
    catch (const PlayerCampaignJsonError& error) {
      rejected = true;
      require(error.stage() == PlayerCampaignJsonStage::GalaxyDecode &&
          error.path() == "$.Galaxy.Systems[0].Id" &&
          error.byte() == text.find("\"bad-native-system-id\""),
          "Developer decode lost original file location inside a deferred array.");
    }
    require(rejected, "Developer save accepted invalid system identity.");
  }
  {
    const auto snapshot=capture_developer_campaign(fast.runtime(),
        {fast.clock().simulation_days(),"test","2050-03-21T00:00:00Z"});
    auto old_common=nlohmann::ordered_json::parse(encode_player_campaign_v17_json(snapshot.campaign));
    old_common["DeveloperSession"]=true;
    auto old_composition=nlohmann::ordered_json::parse(pending);
    old_composition["Campaign"]=std::move(old_common);
    require(old_composition.dump(2)==pending,
        "Developer save composition changed bytes from the serialized player envelope.");
  }
  fast.clock().set_speed(StrategicSpeed::Paused);
  require(fast.advance(100.).completed_substeps.empty()&&saved(fast)==pending,"Paused acceleration consumed or accumulated time.");
  auto restored=restore_developer_campaign_json(load_adaptive_research_strategic_runtime(root),pending);
  StrategicClock restored_clock;restored_clock.restore(restored.simulation_days());
  CampaignFrame loaded(std::move(restored).activate(),restored_clock,CampaignFramePolicy::Developer);
  require(loaded.developer_ticks_behind()==92,"Save/reload lost pending fixed ticks.");
  for(int guard=0;loaded.developer_ticks_behind()&&guard<20;++guard)(void)loaded.advance(0.);
  require(loaded.developer_ticks_behind()==0&&loaded.clock().simulation_days()==25.,"Pending work failed to drain without new wall time.");
  loaded.set_developer_speed(1);
  auto a=nlohmann::json::parse(saved(slow)),b=nlohmann::json::parse(saved(loaded));
  // Equal strategic ticks took different real durations. Activity intentionally
  // follows unscaled real time; all strategic state must still agree.
  require(std::abs(slow.runtime().stellar_activity_day()-25./24.)<1e-12&&
      std::abs(loaded.runtime().stellar_activity_day()-1./24.)<1e-12,"Speed-independent activity elapsed time is wrong");
  a["Campaign"]["Galaxy"].erase("StellarActivityDay");b["Campaign"]["Galaxy"].erase("StellarActivityDay");
  if(a!=b){const auto diff=nlohmann::json::diff(a,b);std::cerr<<diff.dump().substr(0,6500)<<"\n";}
  require(a==b,"1x and 25x diverged after identical authoritative fixed ticks and save/reload.");
  const auto before=saved(loaded);denied=false;try{loaded.set_developer_speed(3);}catch(const std::exception&){denied=true;}
  require(denied&&saved(loaded)==before,"Invalid developer speed mutated simulation state.");
  loaded.set_menu_open(true);require(loaded.advance(10).completed_substeps.empty()&&saved(loaded)==before,"Menu pause advanced accelerated simulation.");
  stellar::engine::FixedClock source(std::chrono::milliseconds(250));source.set_speed(25);(void)source.advance(std::chrono::seconds(1),8);
  stellar::engine::FixedClock copy(std::chrono::milliseconds(1));copy.restore(source.snapshot());
  require(copy.snapshot()==source.snapshot()&&copy.advance(std::chrono::nanoseconds(0),200)==92,"Reusable clock snapshot lost accumulator state.");
  auto bad=copy.snapshot();bad.speed=0;const auto original=copy.snapshot();denied=false;
  try{copy.restore(bad);}catch(const std::exception&){denied=true;}
  require(denied&&copy.snapshot()==original,"Invalid clock restore was not atomic.");

  // A real multi-empire campaign must resume decision schedules, not silently
  // reset/re-plan them on reload. Human ownership is never changed for takeover.
  auto human=make(3),ai=make(3);human.set_developer_speed(1);ai.set_developer_speed(1);
  const int player=ai.runtime().world().campaign().player_civilization_id;
  set_developer_ai_control(ai.runtime(),true);
  (void)human.advance(.25);(void)ai.advance(.25);
  require(human.runtime().research().get_civilization(player).active_projects().empty()&&
      !ai.runtime().research().get_civilization(player).active_projects().empty(),
      "AI takeover did not use the real funded research system independently of the human empire.");
  require(ai.runtime().world().campaign().civilizations.front().is_player&&
      ai.runtime().world().campaign().player_civilization_id==player,"AI takeover changed player identity or ownership.");
  const auto checkpoint=saved(ai);
  auto res=restore_developer_campaign_json(load_adaptive_research_strategic_runtime(root),checkpoint);
  StrategicClock ai_clock;ai_clock.restore(res.simulation_days());
  CampaignFrame resumed(std::move(res).activate(),ai_clock,CampaignFramePolicy::Developer);
  resumed.set_developer_speed(25);
  for(int day=0;day<45;++day){
    for(int tick=0;tick<4;++tick)(void)ai.advance(.25);
    (void)resumed.advance(.04);
    while(resumed.developer_ticks_behind())(void)resumed.advance(0.);
  }
  resumed.set_developer_speed(1);
  auto uninterrupted=nlohmann::json::parse(saved(ai)),reloaded=nlohmann::json::parse(saved(resumed));
  uninterrupted["Campaign"]["Galaxy"].erase("StellarActivityDay");reloaded["Campaign"]["Galaxy"].erase("StellarActivityDay");
  if(uninterrupted!=reloaded)std::cerr<<nlohmann::json::diff(uninterrupted,reloaded).dump().substr(0,6500)<<'\n';
  require(uninterrupted==reloaded,"AI takeover diverged across accelerated ticks/save/reload.");
  set_developer_ai_control(resumed.runtime(),false);
  require(!resumed.runtime().world().campaign().developer_provenance->player_ai_control&&
      resumed.runtime().core().strategic_runtime().snapshot().plans.size()==2,
      "Returning to human control left stale player AI priorities.");
  auto malformed=nlohmann::json::parse(checkpoint);
  malformed["RuntimeContinuation"]["Strategic"]["Plans"][0]["CivilizationId"]=99999;
  denied=false;try{(void)restore_developer_campaign_json(load_adaptive_research_strategic_runtime(root),malformed.dump());}catch(const std::exception&){denied=true;}
  require(denied,"Continuation accepted a plan for a nonexistent civilization.");
  auto single=make();single.set_developer_speed(25);single.clock().set_speed(StrategicSpeed::Paused);
  require(single.can_step_developer(),"Paused developer simulation cannot single step.");
  const auto one=single.step_developer();
  require(one.completed_substeps==std::vector<double>{.25}&&single.clock().simulation_days()==.25&&
      single.clock().speed()==StrategicSpeed::Paused,"Single-step did not advance exactly one paused strategic tick.");
  single.clock().resume();denied=false;try{(void)single.step_developer();}catch(const std::exception&){denied=true;}
  require(denied,"Single-step ran on top of an already running simulation.");

  const auto battle=[&]{
    auto frame=make(2);auto &w=frame.runtime().world().campaign();
    const int a=w.player_civilization_id,b=w.civilizations[1].id,system=w.civilizations[0].home_system_id;
    DiplomacySimulation diplomacy(frame.runtime().diplomacy());
    (void)diplomacy.process_contact_opportunity({a,"dev-battle-b",b,0,system,ContactAwareness::communication_available,ContactCondition::active,true,1.});
    (void)diplomacy.process_contact_opportunity({b,"dev-battle-a",a,0,system,ContactAwareness::communication_available,ContactCondition::active,true,1.});
    diplomacy.declare_war(a,b,0);
    for(int i=0;i<2;++i){
      FleetState fleet;fleet.id=10001+i;fleet.civilization_id=i?b:a;fleet.name=i?"Test opponent":"Test defender";
      fleet.role=FleetRole::Military;fleet.current_system_id=system;
      fleet.combat=create_initial_fleet_combat_state({},fleet.role);
      auto loadout=massive_loadout_from_legacy(get_combat_profile(fleet.combat->profile_id));
      loadout.hull_per_ship=100000;loadout.armor_per_ship=10000;loadout.shield_per_ship=10000;
      for(auto &weapon:loadout.weapons)weapon.range=5000;
      fleet.tactical_loadout=std::move(loadout);w.fleets.push_back(std::move(fleet));
    }
    frame.set_developer_speed(1);require(frame.begin_tactical(10001).accepted,"Could not begin real developer test battle.");
    return frame;
  };
  auto battle_slow=battle(),battle_fast=battle();battle_fast.set_developer_speed(25);
  for(int i=0;i<100;++i)(void)battle_slow.advance(.1);
  const auto busy=battle_fast.advance(.4);
  require(busy.route==CampaignFrameRoute::Tactical&&busy.ready_for_save_capture&&
      battle_fast.developer_ticks_behind()==68,"Tactical acceleration dropped pending time or exceeded its tick budget.");
  const auto battle_checkpoint=saved(battle_fast);
  auto restored_battle=restore_developer_campaign_json(load_adaptive_research_strategic_runtime(root),battle_checkpoint);
  CampaignFrame battle_loaded(std::move(restored_battle).activate(),{},CampaignFramePolicy::Developer);
  require(battle_loaded.developer_ticks_behind()==68,"Tactical pending ticks failed to survive a save/reload.");
  const auto paused_battle=saved(battle_loaded);(void)battle_loaded.advance(10.);
  require(saved(battle_loaded)==paused_battle,"Restored paused tactical battle consumed pending time.");
  battle_loaded.set_tactical_speed(25);
  while(battle_loaded.developer_ticks_behind())(void)battle_loaded.advance(0.);
  battle_loaded.set_developer_speed(1);
  const auto bs=nlohmann::json::parse(saved(battle_slow)),bf=nlohmann::json::parse(saved(battle_loaded));
  if(bs!=bf)std::cerr<<nlohmann::json::diff(bs,bf).dump().substr(0,6500)<<'\n';
  require(bs==bf,"Tactical 1x/25x simulation diverged with pending-save restoration.");
  require(!battle_loaded.runtime().world().campaign().active_combat_encounter->battle.events.empty(),
      "Tactical comparison never exercised real weapon events.");
  battle_loaded.set_tactical_speed(0.);const auto tick_before=battle_loaded.runtime().world().campaign().developer_provenance->simulation.tactical_completed_ticks;
  const auto stepped=battle_loaded.step_developer();
  require(stepped.tactical_accepted_seconds==.1&&battle_loaded.tactical_clock().speed_multiplier()==0.&&
      battle_loaded.runtime().world().campaign().developer_provenance->simulation.tactical_completed_ticks==tick_before+1,
      "Paused tactical single-step did not preserve pause or tick exactly once.");
  std::cout<<"Developer fixed simulation, bounded backlog, pause, save continuity and 1x/25x equivalence passed\n";return 0;
}catch(const std::exception &error){std::cerr<<error.what()<<'\n';return 1;}
