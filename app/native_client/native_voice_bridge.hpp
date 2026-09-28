#pragma once

#include "native_voice.hpp"

#include <stellar/core/campaign_frame.hpp>

#include <cstdint>
#include <optional>
#include <string>
#include <unordered_map>
#include <unordered_set>

namespace stellar::native_voice {

// The GameplayVoiceEventBridge + Main.Voice.cs milestone scan, ported onto the
// native CampaignFrame. The bridge reads only player-observer-authorized state
// (own fleets, the player's diplomacy view, own event payloads) and emits
// NativeGameplayVoiceEvents into the router. It never queries foreign state
// beyond what the player's presentation already surfaces.
class NativeGameplayVoiceBridge final {
public:
  explicit NativeGameplayVoiceBridge(NativeVoiceRouter &router);

  // ResetVoicePresentation port — clears tracking and re-seeds baselines from
  // the freshly active campaign.
  void reset(const core::IntegratedAdaptiveCampaignRuntime &runtime);
  // The opening line fires once per campaign once the menu is closed.
  void observe_opening(core::IntegratedAdaptiveCampaignRuntime &runtime,
                       double simulation_days, VoiceFrequency frequency);
  // ObserveVoiceMilestones + the per-event routes for one advance.
  void observe(const core::CampaignFrameResult &result,
               core::IntegratedAdaptiveCampaignRuntime &runtime,
               double simulation_days, VoiceFrequency frequency);

private:
  struct Scope {
    int player_id{};
    VoiceFrequency frequency{VoiceFrequency::Normal};
    std::int64_t tick{};
    std::string date;
  };

  bool emit_owned(const Scope &, std::string_view key, int civ,
                  std::string_view species, std::string_view identity,
                  std::map<std::string, std::string> variables,
                  bool first = false,
                  std::optional<NativeVoiceOverrides> overrides = std::nullopt);
  bool emit_direct(const Scope &, std::string_view key, int civ,
                   std::string_view species, int recipient,
                   std::string_view identity,
                   std::map<std::string, std::string> variables);
  void observe_fleets(const Scope &,
                      const core::IntegratedAdaptiveCampaignRuntime &);
  void observe_hull(const Scope &,
                    const core::IntegratedAdaptiveCampaignRuntime &);
  void observe_diplomacy(const Scope &,
                         core::IntegratedAdaptiveCampaignRuntime &);
  void observe_economy(const Scope &,
                       const core::IntegratedAdaptiveCampaignRuntime &);
  void observe_logistics(const Scope &,
                         const core::IntegratedAdaptiveCampaignRuntime &);
  void route_events(const Scope &,
                    const core::IntegratedAdaptiveCampaignRuntime &,
                    const core::CampaignFrameResult &);
  void seed_baselines(const core::IntegratedAdaptiveCampaignRuntime &);
  [[nodiscard]] std::string public_system_name(
      const core::IntegratedAdaptiveCampaignRuntime &, int system_id) const;
  [[nodiscard]] std::string species_of(
      const core::IntegratedAdaptiveCampaignRuntime &, int civilization_id,
      std::string_view fallback) const;

  NativeVoiceRouter &router_;
  std::unordered_map<int, bool> fleet_transit_;
  std::unordered_set<std::int64_t> seen_proposals_, seen_diplomacy_;
  std::unordered_set<int> critical_hulls_, critical_logistics_;
  std::unordered_set<std::string> mature_research_;
  bool baseline_{}, opening_{true}, has_interstellar_launch_{},
      has_extrasolar_arrival_{}, has_war_{}, has_first_contact_{};
};

} // namespace stellar::native_voice
