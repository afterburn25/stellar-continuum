#include <stellar/core/developer_campaign_save.hpp>

#include <stellar/engine/atomic_file_write.hpp>

#include <algorithm>
#include <span>
#include <stdexcept>
#include <utility>

namespace stellar::core {
namespace {

void require_save_path(const std::filesystem::path &path) {
  const auto &native = path.native();
  if (native.empty() ||
      std::all_of(native.begin(), native.end(), [](auto c) {
        return c == ' ' || c == '\t' || c == '\r' || c == '\n' || c == '\f' ||
               c == '\v';
      }))
    throw std::invalid_argument("A save path is required.");
}

} // namespace

PreparedDeveloperCampaignSave::PreparedDeveloperCampaignSave(
    PlayerCampaignPayloadV17Dto payload, const bool tools_used)
    : payload_(std::make_shared<const PlayerCampaignPayloadV17Dto>(
          std::move(payload))),
      tools_used_(tools_used) {}

PreparedDeveloperCampaignSave PreparedDeveloperCampaignSave::capture(
    IntegratedAdaptiveCampaignRuntime &campaign,
    const PlayerCampaignCaptureOptions &options) {
  const auto tools_used =
      campaign.world().campaign().developer_provenance
          ? campaign.world().campaign().developer_provenance->tools_used
          : false;
  return PreparedDeveloperCampaignSave(
      capture_developer_campaign_v17(campaign, options), tools_used);
}

const PlayerCampaignPayloadV17Dto &
PreparedDeveloperCampaignSave::payload() const noexcept {
  return *payload_;
}

bool PreparedDeveloperCampaignSave::tools_used() const noexcept {
  return tools_used_;
}

void write_prepared_developer_campaign(
    const std::filesystem::path &path,
    const PreparedDeveloperCampaignSave &prepared,
    const bool preserve_existing_backup) {
  require_save_path(path);
  const auto json =
      encode_developer_campaign_json(prepared.payload(), prepared.tools_used());
  stellar::engine::write_file_atomically(
      path, std::as_bytes(std::span(json.data(), json.size())),
      {preserve_existing_backup});
}

} // namespace stellar::core
