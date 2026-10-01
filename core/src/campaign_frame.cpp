#include <stellar/core/campaign_frame.hpp>
#include <stellar/core/fleet_combat_intelligence.hpp>
#include <stellar/core/developer_campaign.hpp>
#include <stellar/engine/foundation.hpp>

#include <cmath>
#include <exception>
#include <ranges>
#include <utility>
#include <limits>
#include <array>

namespace stellar::core {
namespace {
std::string campaign_advance_exception_message(std::exception_ptr error) {
  if(!error)return {};
  try{std::rethrow_exception(error);}
  catch(const std::exception &e){return e.what();}
  catch(...){return "<non-standard exception>";}
}
} // namespace
std::string_view campaign_advance_failure_phase(
    const IntegratedAdaptiveCampaignAdvanceTrace &trace,
    std::size_t expected_sensor_contacts) noexcept {
  if(!trace.core)return "core";
  if(!trace.research_events)
    return trace.sensor_contacts.size()<expected_sensor_contacts?"sensor":"research";
  if(!trace.diplomacy)return "diplomacy";
  return "chronicle";
}
struct CampaignFrame::Storage {
  std::unique_ptr<IntegratedAdaptiveCampaignRuntime> runtime;
  StrategicClock clock;
  CampaignFramePolicy policy;
  std::optional<CampaignAdvanceFailure> last_advance_failure;
  std::unique_ptr<CampaignMassiveCombat> tactical;
  MassiveCombatClock tactical_clock;
  StrategicSpeed pre_combat_speed{StrategicSpeed::Normal};
  double tactical_resume_speed{1.};
  double tactical_speed_before_menu{1.};
  bool tactical_owns_pause{};
  bool menu_open{};
  bool tactical_menu_pause_owned{};
  stellar::engine::FixedClock developer_clock{std::chrono::milliseconds(250)};
  stellar::engine::FixedClock developer_tactical_clock{std::chrono::milliseconds(100)};
  bool developer_step{};
  bool profiling_enabled{};

  Storage(IntegratedAdaptiveCampaignRuntime value, StrategicClock strategic,
          CampaignFramePolicy frame_policy)
      : runtime(std::make_unique<IntegratedAdaptiveCampaignRuntime>(std::move(value))), clock(std::move(strategic)), policy(frame_policy) {
    if(const auto &provenance=runtime->world().campaign().developer_provenance;provenance&&provenance->simulation.fixed_ticks){
      validate_developer_simulation_state(provenance->simulation);
      const auto &saved=provenance->simulation;
      developer_clock.restore({std::chrono::milliseconds(250),std::chrono::nanoseconds(saved.backlog_nanoseconds),saved.completed_ticks,saved.speed,true});
      developer_tactical_clock.restore({std::chrono::milliseconds(100),std::chrono::nanoseconds(saved.tactical_backlog_nanoseconds),saved.tactical_completed_ticks,saved.speed,true});
    }
    if (runtime->world().campaign().active_combat_encounter &&
        !runtime->world().campaign().active_combat_encounter->reconciled)
      tactical_clock.set_speed(0.);
  }
  CampaignMassiveCombat &tactical_runtime() {
    if (!tactical) tactical = std::make_unique<CampaignMassiveCombat>(
        runtime->diplomacy_runtime().hostility_view().combat_hostility_view());
    return *tactical;
  }
};
CampaignFrame::CampaignFrame(IntegratedAdaptiveCampaignRuntime runtime,
                             StrategicClock clock, CampaignFramePolicy policy)
    : storage_(std::make_unique<Storage>(std::move(runtime), std::move(clock), policy)) {}
CampaignFrame::~CampaignFrame() = default;
CampaignFrame::CampaignFrame(CampaignFrame &&) noexcept = default;
CampaignFrame &CampaignFrame::operator=(CampaignFrame &&) noexcept = default;
IntegratedAdaptiveCampaignRuntime &CampaignFrame::runtime() noexcept { return *storage_->runtime; }
engine::EventHistory &CampaignFrame::history() noexcept { return storage_->runtime->history(); }
const engine::EventHistory &CampaignFrame::history() const noexcept { return storage_->runtime->history(); }
StrategicClock &CampaignFrame::clock() noexcept { return storage_->clock; }
const std::optional<CampaignAdvanceFailure> &CampaignFrame::last_advance_failure() const noexcept {
  return storage_->last_advance_failure;
}
void CampaignFrame::set_profiling_enabled(bool enabled) noexcept {storage_->profiling_enabled=enabled;storage_->runtime->set_profiling_enabled(enabled);}
void CampaignFrame::set_developer_speed(std::uint32_t multiplier){
  auto &s=*storage_;auto &provenance=s.runtime->world().campaign().developer_provenance;
  if(s.policy!=CampaignFramePolicy::Developer||!provenance)
    throw std::logic_error("Accelerated QA simulation requires a developer campaign.");
  auto state=provenance->simulation;state.fixed_ticks=true;state.speed=multiplier;
  validate_developer_simulation_state(state);
  s.developer_clock.set_speed(multiplier);s.developer_tactical_clock.set_speed(multiplier);provenance->simulation=state;
}
std::uint64_t CampaignFrame::developer_ticks_behind() const noexcept {
  const auto &battle=storage_->runtime->world().campaign().active_combat_encounter;
  if(battle&&!battle->reconciled)return static_cast<std::uint64_t>(storage_->developer_tactical_clock.backlog().count()/100000000);
  return static_cast<std::uint64_t>(storage_->developer_clock.backlog().count()/250000000);
}
CampaignFrameResult CampaignFrame::step_developer(){
  auto &s=*storage_;const auto &world=s.runtime->world().campaign();
  if(s.policy!=CampaignFramePolicy::Developer||!world.developer_provenance||!world.developer_provenance->simulation.fixed_ticks)
    throw std::logic_error("Single stepping requires developer fixed simulation.");
  const bool battle=world.active_combat_encounter&&!world.active_combat_encounter->reconciled;
  if(s.menu_open||(battle?s.tactical_clock.speed_multiplier()!=0.:s.clock.speed()!=StrategicSpeed::Paused))
    throw std::logic_error("Pause simulation and close the game menu before single stepping.");
  s.developer_step=true;
  try{auto result=advance(0.);s.developer_step=false;return result;}
  catch(...){s.developer_step=false;throw;}
}
bool CampaignFrame::can_step_developer() const noexcept {
  const auto &s=*storage_;const auto &w=s.runtime->world().campaign();
  if(s.policy!=CampaignFramePolicy::Developer||!w.developer_provenance||
      !w.developer_provenance->simulation.fixed_ticks||s.menu_open)return false;
  const bool battle=w.active_combat_encounter&&!w.active_combat_encounter->reconciled;
  return battle?s.tactical_clock.speed_multiplier()==0.:s.clock.speed()==StrategicSpeed::Paused;
}
std::span<const double> CampaignFrame::tactical_speed_options() const noexcept {
  static constexpr std::array developer{0.,1.,2.,5.,10.,25.};
  const auto &provenance=storage_->runtime->world().campaign().developer_provenance;
  return provenance&&provenance->simulation.fixed_ticks?std::span<const double>(developer):MassiveCombatClock::allowed_speeds();
}
const MassiveCombatClock &CampaignFrame::tactical_clock() const noexcept { return storage_->tactical_clock; }
double CampaignFrame::tactical_resume_speed() const noexcept {
  const auto &p=storage_->runtime->world().campaign().developer_provenance;
  return p&&p->simulation.fixed_ticks?p->simulation.speed:storage_->tactical_resume_speed;
}
void CampaignFrame::set_menu_open(bool open) noexcept { storage_->menu_open = open; }
void CampaignFrame::set_tactical_speed(double speed) {
  auto &s = *storage_; const auto &world = s.runtime->world().campaign();
  if (!world.active_combat_encounter || world.active_combat_encounter->reconciled || !std::ranges::contains(tactical_speed_options(), speed)) return;
  if(world.developer_provenance&&world.developer_provenance->simulation.fixed_ticks){
    if(speed>0)set_developer_speed(static_cast<std::uint32_t>(speed));
    s.tactical_clock.set_speed(speed>0?1.:0.);return;
  }
  s.tactical_clock.set_speed(speed); if (speed > 0.) s.tactical_resume_speed = speed;
}
void CampaignFrame::set_tactical_resume_speed(double speed) {
  if(speed<=0||!std::ranges::contains(tactical_speed_options(),speed))return;
  const auto &p=storage_->runtime->world().campaign().developer_provenance;
  if(p&&p->simulation.fixed_ticks)set_developer_speed(static_cast<std::uint32_t>(speed));
  else storage_->tactical_resume_speed=speed;
}
void CampaignFrame::pause_tactical_for_menu() {
  auto &s = *storage_; const auto &world = s.runtime->world().campaign(); if (!world.active_combat_encounter || world.active_combat_encounter->reconciled || s.tactical_menu_pause_owned) return;
  s.tactical_speed_before_menu = s.tactical_clock.speed_multiplier();
  s.tactical_menu_pause_owned = true; s.tactical_clock.set_speed(0.);
}
void CampaignFrame::resume_tactical_after_menu() {
  auto &s = *storage_; const auto &world = s.runtime->world().campaign(); if (!world.active_combat_encounter || world.active_combat_encounter->reconciled || !s.tactical_menu_pause_owned) return;
  s.tactical_menu_pause_owned = false; s.tactical_clock.set_speed(s.tactical_speed_before_menu);
}
CombatOrderResult CampaignFrame::begin_tactical(int fleet_id) {
  auto &s = *storage_; auto &world = s.runtime->world().campaign();
  auto result = s.tactical_runtime().begin(world, world.player_civilization_id, fleet_id, s.clock.simulation_days(), true);
  if (!result.accepted) return result;
  s.pre_combat_speed = s.clock.speed(); s.clock.set_speed(StrategicSpeed::Paused);
  s.tactical_owns_pause = true; s.tactical_clock.set_speed(1.); return result;
}
MassiveCombatOrderResult CampaignFrame::issue_tactical_order(
    MassiveCombatOrder order) {
  auto &s = *storage_; auto &world = s.runtime->world().campaign();
  return s.tactical_runtime().issue_order(world, world.player_civilization_id,
                                          std::move(order));
}
MassiveCombatSnapshot CampaignFrame::tactical_snapshot() {
  auto &s = *storage_; auto &world = s.runtime->world().campaign();
  if (!world.active_combat_encounter ||
      world.active_combat_encounter->reconciled)
    return {};
  return s.tactical_runtime().observe(
      world, world.player_civilization_id,
      has_combat_scanner(
          s.runtime->research().try_get_civilization(
              world.player_civilization_id)));
}
CampaignFrameResult CampaignFrame::advance(double real_delta_seconds) {
  auto &s = *storage_; auto &world = s.runtime->world().campaign(); CampaignFrameResult result;
  s.last_advance_failure.reset();
  const bool fixed=world.developer_provenance&&world.developer_provenance->simulation.fixed_ticks;
  if(fixed&&(!std::isfinite(real_delta_seconds)||real_delta_seconds<0||real_delta_seconds>=
      static_cast<double>(std::numeric_limits<std::int64_t>::max())/1e9))throw std::invalid_argument("Invalid developer frame time.");
  if (world.active_combat_encounter && !world.active_combat_encounter->reconciled) {
    result.route = CampaignFrameRoute::Tactical;
    if (!s.tactical_owns_pause || s.clock.speed() != StrategicSpeed::Paused) {
      s.pre_combat_speed = s.clock.speed() == StrategicSpeed::Paused ? s.clock.resume_speed() : s.clock.speed();
      s.clock.set_speed(StrategicSpeed::Paused); s.tactical_owns_pause = true;
    }
    const auto delta = std::max(0., std::isfinite(real_delta_seconds) ? real_delta_seconds : 0.);
    if(fixed){
      auto &clock=s.developer_tactical_clock;
      clock.set_paused(s.menu_open||s.tactical_clock.speed_multiplier()==0.);
      std::uint64_t count{};
      if(s.developer_step){clock.step_once();count=1;}
      else count=clock.advance(std::chrono::nanoseconds(static_cast<std::int64_t>(std::round(delta*1e9))),32);
      auto snapshot=clock.snapshot();auto &saved=world.developer_provenance->simulation;
      std::uint64_t completed{};
      try{
        for(;completed<count;){
          const auto begun=s.profiling_enabled?std::chrono::steady_clock::now():std::chrono::steady_clock::time_point{};
          auto events=s.tactical_runtime().advance(world,massive_combat_tick_seconds,
            [&s](int civilization){return has_combat_scanner(s.runtime->research().try_get_civilization(civilization));});
          if(s.profiling_enabled)result.tick_execution_nanoseconds.push_back(static_cast<std::uint64_t>(
              std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::steady_clock::now()-begun).count()));
          result.tactical_events.insert(result.tactical_events.end(),events.begin(),events.end());
          ++completed;result.tactical_accepted_seconds+=massive_combat_tick_seconds;
          if(world.active_combat_encounter->reconciled){
            // No tactical world remains to consume time after reconciliation.
            // Never feed that remainder into strategic time or the next battle.
            snapshot.tick-=count-completed;snapshot.backlog={};clock.restore(snapshot);break;
          }
        }
      }catch(...){
        s.last_advance_failure={"tactical",campaign_advance_exception_message(std::current_exception())};
        snapshot.tick-=count-completed;snapshot.backlog+=std::chrono::milliseconds(static_cast<std::int64_t>(100*(count-completed)));
        clock.restore(snapshot);saved.tactical_completed_ticks=snapshot.tick;saved.tactical_backlog_nanoseconds=snapshot.backlog.count();
        s.tactical_clock.set_speed(0.);throw;
      }
      saved.tactical_completed_ticks=snapshot.tick;saved.tactical_backlog_nanoseconds=snapshot.backlog.count();
      result.ready_for_save_capture=true;
    }else{
      const auto accepted = s.menu_open ? 0. : s.tactical_clock.accept_frame(delta);
      result.tactical_accepted_seconds = accepted;
      try{
        result.tactical_events = accepted > 0. ? s.tactical_runtime().advance(world, accepted,
          [&s](int civilization) { return has_combat_scanner(s.runtime->research().try_get_civilization(civilization)); }) : s.tactical_runtime().reconcile(world);
      }catch(...){
        s.last_advance_failure={"tactical",campaign_advance_exception_message(std::current_exception())};throw;
      }
    }
    if (world.active_combat_encounter && world.active_combat_encounter->reconciled) {
      s.clock.set_speed(s.pre_combat_speed); s.tactical_owns_pause = false; result.tactical_completed = true;
    }
    return result; // Completion consumes this frame.
  }
  result.route = CampaignFrameRoute::Strategic;
  const auto start = s.clock.simulation_days();
  if(world.developer_provenance&&world.developer_provenance->simulation.fixed_ticks){
    if(!std::isfinite(real_delta_seconds)||real_delta_seconds<0||real_delta_seconds>=
        static_cast<double>(std::numeric_limits<std::int64_t>::max())/1e9)
      throw std::invalid_argument("Invalid developer frame time.");
    s.developer_clock.set_paused(s.menu_open||s.clock.speed()==StrategicSpeed::Paused);
    std::uint64_t count{};
    if(s.developer_step){s.developer_clock.step_once();count=1;}
    else count=s.developer_clock.advance(std::chrono::nanoseconds(static_cast<std::int64_t>(std::round(real_delta_seconds*1e9))),8);
    auto snapshot=s.developer_clock.snapshot();
    auto &saved=world.developer_provenance->simulation;
    const double step_days=.25*s.clock.days_per_second();
    for(std::uint64_t index=0;index<count;++index){
      IntegratedAdaptiveCampaignAdvanceTrace trace;
      try{
        const auto begun=s.profiling_enabled?std::chrono::steady_clock::now():std::chrono::steady_clock::time_point{};
        const double end_day=s.clock.simulation_days()+step_days;
        result.strategic_results.push_back(s.runtime->advance(step_days,end_day,&trace));
        if(s.profiling_enabled)result.tick_execution_nanoseconds.push_back(static_cast<std::uint64_t>(
            std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::steady_clock::now()-begun).count()));
        s.clock.record_fixed_advance(step_days,count?real_delta_seconds/static_cast<double>(count):0.,static_cast<double>(snapshot.backlog.count())/1e9*s.clock.days_per_second());
        result.completed_substeps.push_back(step_days);result.completed_end_days.push_back(s.clock.simulation_days());
      }catch(...){
        s.last_advance_failure={std::string(campaign_advance_failure_phase(trace,step_days>0?world.civilizations.size():0)),
                                campaign_advance_exception_message(std::current_exception())};
        // Preserve unprocessed time and stop. Partial subsystem failure must be
        // diagnosed; it must never be hidden by silently dropping pending ticks.
        snapshot.tick-=count-index;snapshot.backlog+=std::chrono::milliseconds(static_cast<std::int64_t>(250*(count-index)));
        s.developer_clock.restore(snapshot);saved.completed_ticks=snapshot.tick;saved.backlog_nanoseconds=snapshot.backlog.count();
        s.clock.set_speed(StrategicSpeed::Paused);throw;
      }
    }
    saved.completed_ticks=snapshot.tick;saved.backlog_nanoseconds=snapshot.backlog.count();
    if(!count)s.clock.record_fixed_advance(0,0,static_cast<double>(snapshot.backlog.count())/1e9*s.clock.days_per_second());
    try{
      result.stellar_weather_launches=s.runtime->advance_stellar_activity(
          std::max(0.,s.clock.simulation_days()-start)*24.);
    }catch(...){
      s.last_advance_failure={"stellar_activity",campaign_advance_exception_message(std::current_exception())};throw;
    }
    result.ready_for_save_capture=true;return result;
  }
  result.completed_substeps = s.policy == CampaignFramePolicy::Developer
      ? advance_developer_frame(s.clock, real_delta_seconds)
      : std::vector<double>{s.clock.advance(real_delta_seconds)};
  double end_day = start;
  for (const auto step : result.completed_substeps) {
    end_day += step;
    result.completed_end_days.push_back(end_day);
    IntegratedAdaptiveCampaignAdvanceTrace trace;
    try{
      result.strategic_results.push_back(s.runtime->advance(step,end_day,&trace));
    }catch(...){
      s.last_advance_failure={std::string(campaign_advance_failure_phase(trace,step>0?world.civilizations.size():0)),
                              campaign_advance_exception_message(std::current_exception())};throw;
    }
  }
  try{
    result.stellar_weather_launches=s.runtime->advance_stellar_activity(
        std::max(0.,s.clock.simulation_days()-start)*24.);
  }catch(...){
    s.last_advance_failure={"stellar_activity",campaign_advance_exception_message(std::current_exception())};throw;
  }
  result.ready_for_save_capture = true;
  return result;
}
}  // namespace stellar::core
