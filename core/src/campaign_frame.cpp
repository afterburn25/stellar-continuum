#include <stellar/core/campaign_frame.hpp>
#include <stellar/core/fleet_combat_intelligence.hpp>

#include <cmath>
#include <ranges>
#include <utility>

namespace stellar::core {
struct CampaignFrame::Storage {
  std::unique_ptr<IntegratedAdaptiveCampaignRuntime> runtime;
  StrategicClock clock;
  CampaignFramePolicy policy;
  std::unique_ptr<CampaignMassiveCombat> tactical;
  MassiveCombatClock tactical_clock;
  StrategicSpeed pre_combat_speed{StrategicSpeed::Normal};
  double tactical_resume_speed{1.};
  double tactical_speed_before_menu{1.};
  bool tactical_owns_pause{};
  bool menu_open{};
  bool tactical_menu_pause_owned{};

  Storage(IntegratedAdaptiveCampaignRuntime value, StrategicClock strategic,
          CampaignFramePolicy frame_policy)
      : runtime(std::make_unique<IntegratedAdaptiveCampaignRuntime>(std::move(value))), clock(std::move(strategic)), policy(frame_policy) {
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
StrategicClock &CampaignFrame::clock() noexcept { return storage_->clock; }
const MassiveCombatClock &CampaignFrame::tactical_clock() const noexcept { return storage_->tactical_clock; }
double CampaignFrame::tactical_resume_speed() const noexcept { return storage_->tactical_resume_speed; }
void CampaignFrame::set_menu_open(bool open) noexcept { storage_->menu_open = open; }
void CampaignFrame::set_tactical_speed(double speed) {
  auto &s = *storage_; const auto &world = s.runtime->world().campaign();
  if (!world.active_combat_encounter || world.active_combat_encounter->reconciled || !std::ranges::contains(MassiveCombatClock::allowed_speeds(), speed)) return;
  s.tactical_clock.set_speed(speed); if (speed > 0.) s.tactical_resume_speed = speed;
}
void CampaignFrame::set_tactical_resume_speed(double speed) { if (speed > 0. && std::ranges::contains(MassiveCombatClock::allowed_speeds(), speed)) storage_->tactical_resume_speed = speed; }
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
  auto result = s.tactical_runtime().begin(world, world.player_civilization_id, fleet_id, s.clock.simulation_days());
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
  if (world.active_combat_encounter && !world.active_combat_encounter->reconciled) {
    result.route = CampaignFrameRoute::Tactical;
    if (!s.tactical_owns_pause || s.clock.speed() != StrategicSpeed::Paused) {
      s.pre_combat_speed = s.clock.speed() == StrategicSpeed::Paused ? s.clock.resume_speed() : s.clock.speed();
      s.clock.set_speed(StrategicSpeed::Paused); s.tactical_owns_pause = true;
    }
    const auto delta = std::max(0., std::isfinite(real_delta_seconds) ? real_delta_seconds : 0.);
    const auto accepted = s.menu_open ? 0. : s.tactical_clock.accept_frame(delta);
    result.tactical_accepted_seconds = accepted;
    result.tactical_events = accepted > 0. ? s.tactical_runtime().advance(world, accepted,
      [&s](int civilization) { return has_combat_scanner(s.runtime->research().try_get_civilization(civilization)); }) : s.tactical_runtime().reconcile(world);
    if (world.active_combat_encounter && world.active_combat_encounter->reconciled) {
      s.clock.set_speed(s.pre_combat_speed); s.tactical_owns_pause = false; result.tactical_completed = true;
    }
    return result; // Completion consumes this frame.
  }
  result.route = CampaignFrameRoute::Strategic;
  const auto start = s.clock.simulation_days();
  result.completed_substeps = s.policy == CampaignFramePolicy::Developer
      ? advance_developer_frame(s.clock, real_delta_seconds)
      : std::vector<double>{s.clock.advance(real_delta_seconds)};
  double end_day = start;
  for (const auto step : result.completed_substeps) {
    end_day += step;
    result.completed_end_days.push_back(end_day);
    result.strategic_results.push_back(s.runtime->advance(step, end_day));
  }
  result.ready_for_save_capture = true;
  return result;
}
}  // namespace stellar::core
