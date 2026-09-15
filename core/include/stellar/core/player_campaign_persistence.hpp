#pragma once

#include <stellar/core/adaptive_research_campaign.hpp>
#include <stellar/core/diplomacy_state.hpp>
#include <stellar/core/galaxy_payload_persistence.hpp>
#include <stellar/core/integrated_adaptive_campaign.hpp>

#include <functional>
#include <optional>
#include <memory>
#include <stdexcept>
#include <string>
#include <string_view>

namespace stellar::core {

class RestoredPlayerCampaignV17;
struct PlayerCampaignRestoreHooks {
  std::function<void()> before_diplomacy_references;
  std::function<void()> before_research_restore;
  std::function<void()> before_diplomacy_restore;
};
namespace detail {
[[nodiscard]] RestoredPlayerCampaignV17 finalize_restored_player_campaign_v17(
    AdaptiveResearchStrategicRuntime,
    RestoredGalaxyPayloadV16,
    std::function<AdaptiveResearchCampaignSnapshot()>,
    const DiplomacyStateSnapshot &, const PlayerCampaignRestoreHooks & = {});
}

class PlayerCampaignPersistenceDataError final : public std::runtime_error {
public:
  explicit PlayerCampaignPersistenceDataError(std::string message);
  PlayerCampaignPersistenceDataError(std::string message,
                                     std::string inner_type,
                                     std::string inner_message);
  [[nodiscard]] const std::optional<std::string> &inner_type() const noexcept;
  [[nodiscard]] const std::optional<std::string> &
  inner_message() const noexcept;

private:
  std::optional<std::string> inner_type_;
  std::optional<std::string> inner_message_;
};

class PlayerCampaignPersistenceOperationError final
    : public std::runtime_error {
public:
  using std::runtime_error::runtime_error;
};

class PlayerCampaignPersistenceRangeError final : public std::out_of_range {
public:
  using std::out_of_range::out_of_range;
};

struct PlayerCampaignCaptureOptions {
  double simulation_days{};
  std::string game_version;
  std::string saved_at_utc;
};

struct PlayerCampaignPayloadV17Dto {
  static constexpr int current_format_version = 17;

  int format_version{current_format_version};
  std::optional<int> galaxy_format_version{
      GalaxyPayloadV16Dto::current_format_version};
  GalaxyPayloadV16Dto galaxy;
  std::optional<DiplomacyStateSnapshot> diplomacy;
  std::optional<AdaptiveResearchCampaignSnapshot> adaptive_research;
};

class DiplomacyCampaignReferenceValidator final {
public:
  static void validate(const FreshCampaignState &galaxy,
                       const DiplomacyStateSnapshot &snapshot);
};

// Stable persistence owner. Restoration itself performs no host clock
// activation. Moving the owner preserves member addresses; move-assignment
// invalidates previously returned references. activate() consumes the owner and
// enters the existing integrated runtime boundary, where simulation-time clock
// validation belongs.
class RestoredPlayerCampaignV17 final {
public:
  ~RestoredPlayerCampaignV17();
  RestoredPlayerCampaignV17(RestoredPlayerCampaignV17 &&) noexcept;
  RestoredPlayerCampaignV17 &
  operator=(RestoredPlayerCampaignV17 &&) noexcept;
  RestoredPlayerCampaignV17(const RestoredPlayerCampaignV17 &) = delete;
  RestoredPlayerCampaignV17 &
  operator=(const RestoredPlayerCampaignV17 &) = delete;

  [[nodiscard]] FreshCampaignState &galaxy() noexcept;
  [[nodiscard]] const FreshCampaignState &galaxy() const noexcept;
  [[nodiscard]] DiplomacyState &diplomacy() noexcept;
  [[nodiscard]] const DiplomacyState &diplomacy() const noexcept;
  [[nodiscard]] AdaptiveResearchCampaignState &research() noexcept;
  [[nodiscard]] const AdaptiveResearchCampaignState &research() const noexcept;
  [[nodiscard]] const AdaptiveResearchStrategicRuntime &
  research_runtime() const noexcept;
  [[nodiscard]] double simulation_days() const noexcept;
  [[nodiscard]] std::string_view game_version() const noexcept;
  [[nodiscard]] std::string_view saved_at_utc() const noexcept;

  // Always consumes this owner. After return or exception, the source object
  // may only be destroyed or move-assigned; no previously returned borrow is
  // valid. The research state is destroyed before its runtime is moved.
  [[nodiscard]] IntegratedAdaptiveCampaignRuntime activate() &&;

private:
  struct Storage;
  explicit RestoredPlayerCampaignV17(std::unique_ptr<Storage>) noexcept;
  std::unique_ptr<Storage> storage_;

  friend RestoredPlayerCampaignV17 restore_player_campaign_v17(
      AdaptiveResearchStrategicRuntime,
      const PlayerCampaignPayloadV17Dto &);
  friend RestoredPlayerCampaignV17
  detail::finalize_restored_player_campaign_v17(
       AdaptiveResearchStrategicRuntime,
       RestoredGalaxyPayloadV16,
       std::function<AdaptiveResearchCampaignSnapshot()>,
       const DiplomacyStateSnapshot &, const PlayerCampaignRestoreHooks &);
};

[[nodiscard]] PlayerCampaignPayloadV17Dto capture_player_campaign_v17(
    IntegratedAdaptiveCampaignRuntime &campaign,
    const PlayerCampaignCaptureOptions &options);

[[nodiscard]] RestoredPlayerCampaignV17 restore_player_campaign_v17(
    AdaptiveResearchStrategicRuntime research_runtime,
    const PlayerCampaignPayloadV17Dto &payload);

} // namespace stellar::core
