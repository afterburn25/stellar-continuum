#pragma once

#include <stellar/core/campaign_frame.hpp>
#include <stellar/core/player_campaign_persistence.hpp>
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

// Capture is performed by the simulation owner. The resulting value owns a
// detached, immutable Player17 DTO; worker jobs cannot borrow the live world.
// A Developer capture shares the same canonical payload and only carries the
// envelope markers the developer writer needs.
class PreparedPlayerCampaignSave final {
public:
  static PreparedPlayerCampaignSave capture(
      IntegratedAdaptiveCampaignRuntime &, const PlayerCampaignCaptureOptions &);
  // Source: DeveloperCampaignPersistenceService.Capture — the campaign must
  // carry developer provenance; the envelope records its ToolsUsed flag.
  static PreparedPlayerCampaignSave capture_developer(
      IntegratedAdaptiveCampaignRuntime &, const PlayerCampaignCaptureOptions &);
  [[nodiscard]] const PlayerCampaignPayloadV17Dto &payload() const noexcept;
  [[nodiscard]] bool is_developer() const noexcept;
  [[nodiscard]] bool tools_used() const noexcept;
private:
  explicit PreparedPlayerCampaignSave(PlayerCampaignPayloadV17Dto,
                                      bool developer = false,
                                      bool tools_used = false);
  std::shared_ptr<const PlayerCampaignPayloadV17Dto> payload_;
  bool developer_{};
  bool tools_used_{};
};

void write_prepared_player_campaign(const std::filesystem::path &,
                                   const PreparedPlayerCampaignSave &,
                                   bool preserve_existing_backup);

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

// Invoked synchronously on the simulation thread with each captured payload
// (autosave and manual saves). Used by replay recording/verification to hash
// the canonical state at deterministic points.
using PlayerCampaignCaptureObserver = std::function<void(
    double simulation_days, const PlayerCampaignPayloadV17Dto &)>;

// A single simulation-thread owner controls one immutable background write.
// Configure/replace requires draining first. It neither owns nor advances the
// simulation. Call after_frame only with that frame's actual returned result.
class PlayerCampaignSaveController final {
public:
  // developer_mode selects the Developer envelope capture path; the writer
  // must then be a developer-envelope writer.
  explicit PlayerCampaignSaveController(
      CampaignAutosavePolicy policy = {},
      PlayerCampaignPreparedWriter writer = write_prepared_player_campaign,
      bool developer_mode = false);
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
  // The observer runs before the write job is submitted; it must be cheap
  // relative to a full payload capture and must not throw into the writer.
  void set_capture_observer(PlayerCampaignCaptureObserver observer);
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
  [[nodiscard]] PreparedPlayerCampaignSave capture_prepared(
      IntegratedAdaptiveCampaignRuntime &,
      const PlayerCampaignCaptureOptions &) const;
  PlayerCampaignSaveResult failure(std::exception_ptr, double captured_day,
                                  const std::filesystem::path &, bool) const;
  std::thread::id owner_{std::this_thread::get_id()};
  CampaignAutosaveScheduler scheduler_;
  PlayerCampaignPreparedWriter writer_;
  stellar::engine::JobSystem jobs_{1};
  std::filesystem::path path_;
  PlayerCampaignCaptureObserver capture_observer_;
  std::uint64_t revision_{};
  bool configured_{};
  bool developer_mode_{};
  bool preserve_backup_{};
  std::optional<Pending> pending_;
};
} // namespace stellar::core
