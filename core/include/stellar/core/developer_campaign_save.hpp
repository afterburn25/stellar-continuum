#pragma once

#include <stellar/core/developer_campaign_json.hpp>

#include <filesystem>
#include <memory>

namespace stellar::core {

// Source: Game.Persistence.DeveloperCampaignPersistenceService — capture on
// the simulation-owner thread produces an immutable envelope payload; the
// prepared value is safe to hand to a detached writer.
class PreparedDeveloperCampaignSave final {
public:
  static PreparedDeveloperCampaignSave capture(
      IntegratedAdaptiveCampaignRuntime &campaign,
      const PlayerCampaignCaptureOptions &options);
  [[nodiscard]] const PlayerCampaignPayloadV17Dto &payload() const noexcept;
  [[nodiscard]] bool tools_used() const noexcept;

private:
  explicit PreparedDeveloperCampaignSave(PlayerCampaignPayloadV17Dto,
                                         bool tools_used);
  std::shared_ptr<const PlayerCampaignPayloadV17Dto> payload_;
  bool tools_used_{};
};

void write_prepared_developer_campaign(
    const std::filesystem::path &path,
    const PreparedDeveloperCampaignSave &prepared,
    bool preserve_existing_backup);

} // namespace stellar::core
