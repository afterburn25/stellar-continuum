#pragma once

#include <stellar/core/campaign_massive_combat.hpp>
#include <stellar/core/integrated_adaptive_campaign.hpp>
#include <stellar/core/massive_combat_engine.hpp>
#include <stellar/core/strategic_clock.hpp>

#include <memory>
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

 private:
  struct Storage;
  std::unique_ptr<Storage> storage_;
};
}  // namespace stellar::core
