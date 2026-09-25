#pragma once

#include <stellar/core/campaign_massive_combat.hpp>
#include <stellar/core/integrated_adaptive_campaign.hpp>
#include <stellar/core/massive_combat_engine.hpp>
#include <stellar/core/strategic_clock.hpp>
#include <stellar/engine/history.hpp>

#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace stellar::core {

enum class CampaignFramePolicy { Player, Developer };
enum class CampaignFrameRoute { Tactical, Strategic };

struct CampaignFrameResult {
  CampaignFrameRoute route{};
  std::vector<double> completed_substeps;
  std::vector<double> completed_end_days;
  std::vector<IntegratedAdaptiveCampaignStepResult> strategic_results;
  std::vector<CombatEvent> tactical_events;
  std::vector<TravelingCmeLaunch> stellar_weather_launches;
  double tactical_accepted_seconds{};
  bool tactical_completed{};
  // True after all selected strategic (or developer tactical) steps complete.
  bool ready_for_save_capture{};
  // Opt-in wall-clock execution measurements, never serialized or used by
  // simulation decisions. One sample per completed authoritative tick.
  std::vector<std::uint64_t> tick_execution_nanoseconds;
};

// Attributes a retained advance trace to the first phase whose output is
// absent. `expected_sensor_contacts` is the campaign civilization count
// when the step advanced positive time (0 for zero-day steps, where the
// sensor loop is skipped by design); a partially filled contact vector
// means the sensor loop itself threw. All four phase outputs present means
// the throw came from post-step chronicle recording.
[[nodiscard]] std::string_view campaign_advance_failure_phase(
    const IntegratedAdaptiveCampaignAdvanceTrace &trace,
    std::size_t expected_sensor_contacts) noexcept;

// Diagnosis for an authoritative step that threw inside advance(). The
// phase names the strategic phase whose output is absent from the retained
// trace, or the tactical route when the combat advance threw.
struct CampaignAdvanceFailure {
  std::string phase;
  std::string message;
};

// Reconstructed headless adapter for Main's frame routing. Trusted bootstrap
// alignment (runtime world and clock day) is the caller's responsibility.
class CampaignFrame final {
 public:
  CampaignFrame(IntegratedAdaptiveCampaignRuntime runtime, StrategicClock clock,
                CampaignFramePolicy policy);
  ~CampaignFrame();
  CampaignFrame(CampaignFrame &&) noexcept;
  CampaignFrame &operator=(CampaignFrame &&) noexcept;
  CampaignFrame(const CampaignFrame &) = delete;
  CampaignFrame &operator=(const CampaignFrame &) = delete;

  [[nodiscard]] IntegratedAdaptiveCampaignRuntime &runtime() noexcept;
  [[nodiscard]] StrategicClock &clock() noexcept;
  void set_profiling_enabled(bool enabled) noexcept;
  // Only explicitly marked developer sessions may opt into accelerated fixed
  // ticks. Existing Player and legacy developer replay policies are unchanged.
  void set_developer_speed(std::uint32_t multiplier);
  [[nodiscard]] std::uint64_t developer_ticks_behind() const noexcept;
  [[nodiscard]] CampaignFrameResult step_developer();
  [[nodiscard]] bool can_step_developer() const noexcept;
  [[nodiscard]] std::span<const double> tactical_speed_options() const noexcept;
  [[nodiscard]] const MassiveCombatClock &tactical_clock() const noexcept;
  [[nodiscard]] double tactical_resume_speed() const noexcept;
  void set_tactical_speed(double speed);
  void set_menu_open(bool open) noexcept;
  void set_tactical_resume_speed(double speed);
  void pause_tactical_for_menu();
  void resume_tactical_after_menu();
  [[nodiscard]] CombatOrderResult begin_tactical(int fleet_id);
  // Mirrors Main.UiIssueMassiveCombatOrder / UiMassiveCombatSnapshot for the
  // player observer. The snapshot is empty when no unreconciled encounter is
  // active; orders are rejected with the reference message in that case.
  [[nodiscard]] MassiveCombatOrderResult
  issue_tactical_order(MassiveCombatOrder order);
  [[nodiscard]] MassiveCombatSnapshot tactical_snapshot();
  [[nodiscard]] CampaignFrameResult advance(double real_delta_seconds);
  // Set when the previous advance attempt threw mid-step; cleared by the
  // next attempt. Diagnostics read it after catching advance().
  [[nodiscard]] const std::optional<CampaignAdvanceFailure> &
  last_advance_failure() const noexcept;
  // The campaign chronicle: every completed advance's emitted events,
  // owned by the runtime and serialized with the save payload.
  [[nodiscard]] engine::EventHistory &history() noexcept;
  [[nodiscard]] const engine::EventHistory &history() const noexcept;

 private:
  struct Storage;
  std::unique_ptr<Storage> storage_;
};
}  // namespace stellar::core
