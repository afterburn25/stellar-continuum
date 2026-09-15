#pragma once

#include <stellar/core/adaptive_research_campaign_simulation.hpp>
#include <stellar/core/adaptive_research_capability_adapters.hpp>
#include <stellar/core/campaign_coordinator.hpp>
#include <stellar/core/diplomacy_runtime.hpp>
#include <stellar/core/fleet_combat_intelligence.hpp>

#include <memory>
#include <optional>
#include <vector>

namespace stellar::core {

struct IntegratedSensorContactRecordingResult {
  int civilization_id{};
  int recorded_contacts{};
};

struct IntegratedAdaptiveCampaignStepResult {
  SimulationStepResult core;
  std::vector<IntegratedSensorContactRecordingResult> sensor_contacts;
  std::vector<AdaptiveResearchCampaignEvent> research_events;
  DiplomacyCampaignRuntimeStepResult diplomacy;
};

// Retains only phase outputs that completed during the most recent advance.
// This makes source-compatible partial mutation and phase order inspectable
// when a later phase throws and the ordinary return value cannot be produced.
struct IntegratedAdaptiveCampaignAdvanceTrace {
  std::optional<SimulationStepResult> core;
  std::vector<IntegratedSensorContactRecordingResult> sensor_contacts;
  std::optional<std::vector<AdaptiveResearchCampaignEvent>> research_events;
  std::optional<DiplomacyCampaignRuntimeStepResult> diplomacy;
};

// Stable owning composition reconstructed from Main.CoreIntegration.cs. This
// is a plain simulation owner, not a Godot Main invocation or player-save
// compatibility surface. Moving the outer owner preserves all final member
// addresses. Moving or replacing an individually borrowed member invalidates
// its dependents.
class IntegratedAdaptiveCampaignRuntime final {
public:
  [[nodiscard]] static IntegratedAdaptiveCampaignRuntime create_fresh(
      AdaptiveResearchStrategicRuntime research_runtime,
      FreshCampaignState world, DiplomacyState diplomacy = {},
      double current_simulation_day = 0.0);
  [[nodiscard]] static IntegratedAdaptiveCampaignRuntime restore_research(
      AdaptiveResearchStrategicRuntime research_runtime,
      FreshCampaignState world,
      const AdaptiveResearchCampaignSnapshot &research_snapshot,
      DiplomacyState diplomacy, double current_simulation_day);

  ~IntegratedAdaptiveCampaignRuntime();
  IntegratedAdaptiveCampaignRuntime(IntegratedAdaptiveCampaignRuntime &&) noexcept;
  IntegratedAdaptiveCampaignRuntime &
  operator=(IntegratedAdaptiveCampaignRuntime &&) noexcept;
  IntegratedAdaptiveCampaignRuntime(const IntegratedAdaptiveCampaignRuntime &) = delete;
  IntegratedAdaptiveCampaignRuntime &
  operator=(const IntegratedAdaptiveCampaignRuntime &) = delete;

  [[nodiscard]] CampaignSimulationState &world() noexcept;
  [[nodiscard]] const CampaignSimulationState &world() const noexcept;
  [[nodiscard]] AdaptiveResearchCampaignState &research() noexcept;
  [[nodiscard]] const AdaptiveResearchCampaignState &research() const noexcept;
  [[nodiscard]] DiplomacyState &diplomacy() noexcept;
  [[nodiscard]] const DiplomacyState &diplomacy() const noexcept;
  [[nodiscard]] const AdaptiveResearchStrategicRuntime &
  research_runtime() const noexcept;
  [[nodiscard]] GalaxySimulationStepCoordinator &core() noexcept;
  [[nodiscard]] DiplomacyCampaignRuntimeCoordinator &
  diplomacy_runtime() noexcept;
  [[nodiscard]] std::span<const FleetPowerObservation>
  combat_intelligence() const noexcept;

  [[nodiscard]] IntegratedAdaptiveCampaignStepResult
  advance(double elapsed_days, double absolute_end_day,
          IntegratedAdaptiveCampaignAdvanceTrace *trace = nullptr);

private:
  struct Storage;
  explicit IntegratedAdaptiveCampaignRuntime(std::unique_ptr<Storage>) noexcept;
  std::unique_ptr<Storage> storage_;
};

} // namespace stellar::core
