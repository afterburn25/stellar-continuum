#pragma once

#include <stellar/core/campaign_frame.hpp>
#include <stellar/core/player_campaign_persistence.hpp>
#include <stellar/core/developer_campaign.hpp>
#include <stellar/engine/foundation.hpp>

#include <cstdint>
#include <filesystem>
#include <functional>
#include <future>
#include <memory>
#include <optional>
#include <string>
#include <thread>

namespace stellar::core {

enum class CampaignSaveKind { Player, Developer };
[[nodiscard]] bool is_developer_campaign_save_path(const std::filesystem::path &);

// Capture is performed by the simulation owner. The resulting value owns a
// detached, immutable Player17 DTO; worker jobs cannot borrow the live world.
class PreparedPlayerCampaignSave final {
public:
  static PreparedPlayerCampaignSave capture(
      IntegratedAdaptiveCampaignRuntime &, const PlayerCampaignCaptureOptions &);
  static PreparedPlayerCampaignSave capture_developer(
      IntegratedAdaptiveCampaignRuntime &, const PlayerCampaignCaptureOptions &);
  [[nodiscard]] const PlayerCampaignPayloadV17Dto &payload() const;
  [[nodiscard]] const DeveloperCampaignPayload &developer_payload() const;
  [[nodiscard]] CampaignSaveKind kind() const noexcept;
private:
  explicit PreparedPlayerCampaignSave(PlayerCampaignPayloadV17Dto);
  explicit PreparedPlayerCampaignSave(DeveloperCampaignPayload);
  std::shared_ptr<const PlayerCampaignPayloadV17Dto> payload_;
  std::shared_ptr<const DeveloperCampaignPayload> developer_payload_;
};

void write_prepared_player_campaign(const std::filesystem::path &,
                                   const PreparedPlayerCampaignSave &,
                                   bool preserve_existing_backup);
// Explicitly typed captures use the same atomic writer and backup policy.
void write_prepared_campaign(const std::filesystem::path &,
                             const PreparedPlayerCampaignSave &, bool);

struct PlayerCampaignSaveResult {
  bool succeeded{};
  std::filesystem::path path;
  double captured_day{};
  bool preserved_backup{};
  std::string error_type;
  std::string error_message;
};

using PlayerCampaignPreparedWriter = std::function<void(
    const std::filesystem::path &, const PreparedPlayerCampaignSave &, bool)>;

// A single simulation-thread owner controls one immutable background write.
// Configure/replace requires draining first. It neither owns nor advances the
// simulation. Call after_frame only with that frame's actual returned result.
class PlayerCampaignSaveController final {
public:
  explicit PlayerCampaignSaveController(
      CampaignAutosavePolicy policy = {},
      PlayerCampaignPreparedWriter writer = write_prepared_campaign,
      CampaignSaveKind kind = CampaignSaveKind::Player);
  ~PlayerCampaignSaveController();
  PlayerCampaignSaveController(const PlayerCampaignSaveController &) = delete;
  PlayerCampaignSaveController &operator=(const PlayerCampaignSaveController &) = delete;

  void configure(std::filesystem::path path, std::uint64_t revision,
                 double day, bool recovered_from_backup);
  [[nodiscard]] std::optional<PlayerCampaignSaveResult> after_frame(
      CampaignFrame &, const CampaignFrameResult &,
      const std::string &game_version, const std::string &saved_at_utc);
  [[nodiscard]] std::optional<PlayerCampaignSaveResult> complete(
      double current_day, const std::filesystem::path &current_path,
      std::uint64_t current_revision, bool wait = false);
  // The host must consume/report any pending completion before this call.
  [[nodiscard]] PlayerCampaignSaveResult save_manual(
      IntegratedAdaptiveCampaignRuntime &, const PlayerCampaignCaptureOptions &);
  // Admits one detached manual save to the existing writer. Nullopt means the
  // capture and job submission succeeded and completion remains pending.
  [[nodiscard]] std::optional<PlayerCampaignSaveResult> begin_manual(
      IntegratedAdaptiveCampaignRuntime &, const PlayerCampaignCaptureOptions &);
  [[nodiscard]] bool pending() const noexcept;
  [[nodiscard]] bool preserves_recovered_backup() const noexcept;
  [[nodiscard]] double next_due_day() const noexcept;
  [[nodiscard]] const std::filesystem::path &path() const noexcept;
  [[nodiscard]] std::uint64_t revision() const noexcept;

private:
  struct Pending {
    std::future<void> task;
    std::filesystem::path path;
    std::uint64_t revision{};
    double captured_day{};
    bool preserved_backup{};
  };
  void require_owner() const;
  [[nodiscard]] PreparedPlayerCampaignSave capture(
      IntegratedAdaptiveCampaignRuntime &, const PlayerCampaignCaptureOptions &) const;
  void submit(PreparedPlayerCampaignSave, double captured_day, bool preserve);
  PlayerCampaignSaveResult failure(std::exception_ptr, double captured_day,
                                  const std::filesystem::path &, bool) const;
  std::thread::id owner_{std::this_thread::get_id()};
  CampaignAutosaveScheduler scheduler_;
  PlayerCampaignPreparedWriter writer_;
  CampaignSaveKind kind_{};
  stellar::engine::JobSystem jobs_{1};
  std::filesystem::path path_;
  std::uint64_t revision_{};
  bool configured_{};
  bool preserve_backup_{};
  std::optional<Pending> pending_;
};
} // namespace stellar::core
