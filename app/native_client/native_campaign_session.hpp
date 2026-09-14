#pragma once

#include <stellar/core/campaign_frame.hpp>
#include <stellar/core/fresh_campaign.hpp>
#include <stellar/core/lane_network.hpp>
#include <stellar/core/player_campaign_recovery.hpp>
#include <stellar/core/player_campaign_save.hpp>

#include <cstdint>
#include <filesystem>
#include <functional>
#include <future>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <thread>
#include <unordered_map>
#include <vector>

namespace stellar::native_map {

enum class SessionNoticeKind {
  None,
  Loading,
  Saving,
  Saved,
  Loaded,
  Recovered,
  Failure,
};

struct SessionNotice {
  SessionNoticeKind kind{};
  std::string message;
  double progress{};
};

struct NativeCampaignCache {
  // Pointers borrow the current live CampaignFrame. They become invalid as soon
  // as service() successfully activates another campaign. Callers must compare
  // generation and reacquire every pointer after any activating service call.
  std::unordered_map<int, const stellar::core::StellarSystem *> systems_by_id;
  std::vector<stellar::core::InterstellarLane> lanes;
  std::uint64_t generation{};
};

using NativeCampaignLoader = std::function<stellar::core::LoadedPlayerCampaignV17(
    const std::filesystem::path &,
    const stellar::core::PlayerCampaignRuntimeFactory &,
    const std::function<void(
        const stellar::core::PlayerCampaignRestorationProgress &)> &)>;

struct NativeCampaignSessionDependencies {
  stellar::core::PlayerCampaignPreparedWriter save_writer{
      stellar::core::write_prepared_player_campaign};
  NativeCampaignLoader loader{stellar::core::load_existing_player_campaign_v17};
  std::function<void(stellar::core::CampaignFrame &)> validate_candidate;
};

class NativeCampaignSession final {
public:
  static std::unique_ptr<NativeCampaignSession> create_fresh(
      stellar::core::IntegratedAdaptiveCampaignRuntime runtime,
      std::filesystem::path research_root, std::filesystem::path save_path,
      std::string game_version,
      NativeCampaignSessionDependencies dependencies = {});

  // Explicit startup load. It either returns the requested restored campaign or
  // throws; it never creates a replacement campaign or writes either save file.
  static std::unique_ptr<NativeCampaignSession> load_startup(
      std::filesystem::path research_root, std::filesystem::path save_path,
      std::string game_version,
      const std::function<void(
          const stellar::core::PlayerCampaignRestorationProgress &)> &progress = {},
      NativeCampaignSessionDependencies dependencies = {});

  // Owner-thread activation boundary for a detached worker load. The worker
  // may produce LoadedPlayerCampaignV17, but must not construct this session.
  static std::unique_ptr<NativeCampaignSession> create_loaded(
      stellar::core::LoadedPlayerCampaignV17 loaded,
      std::filesystem::path research_root, std::filesystem::path save_path,
      std::string game_version,
      NativeCampaignSessionDependencies dependencies = {});

  // Destruction waits for background work but cannot report its result. Durable
  // shutdown must use request_exit()/service() and observe exit_ready().
  ~NativeCampaignSession();
  NativeCampaignSession(const NativeCampaignSession &) = delete;
  NativeCampaignSession &operator=(const NativeCampaignSession &) = delete;

  // The session and every returned reference are confined to the thread that
  // created the session. Only detached loader/save work runs in the background.
  [[nodiscard]] stellar::core::CampaignFrame &frame();
  [[nodiscard]] const NativeCampaignCache &cache() const;
  [[nodiscard]] const std::filesystem::path &save_path() const;
  [[nodiscard]] const SessionNotice &notice() const;
  [[nodiscard]] bool load_pending() const;
  [[nodiscard]] bool exit_ready() const;

  [[nodiscard]] stellar::core::CampaignFrameResult
  advance(double real_delta_seconds, const std::string &saved_at_utc);
  void request_save();
  void request_load();
  void request_exit();

  // Runs deferred main-thread capture and activation work. Returns true only
  // when a fully validated load candidate replaced the live session.
  [[nodiscard]] bool service(const std::string &saved_at_utc,
                             bool menu_open);

private:
  struct Live;
  struct LoadProgress;
  struct PendingLoad;

  NativeCampaignSession(std::unique_ptr<Live>, std::filesystem::path,
                        std::filesystem::path, std::string,
                        NativeCampaignSessionDependencies);
  static NativeCampaignCache build_cache(stellar::core::CampaignFrame &,
                                         std::uint64_t generation);
  [[nodiscard]] stellar::core::PlayerCampaignRuntimeFactory
  runtime_factory() const;
  [[nodiscard]] std::unique_ptr<Live> activate(
      stellar::core::LoadedPlayerCampaignV17, bool menu_open,
      std::uint64_t revision, std::uint64_t cache_generation);
  void begin_load();
  void publish_save_result(const stellar::core::PlayerCampaignSaveResult &,
                           std::string success_message);
  void publish_failure(std::string message);
  void require_owner() const;
  [[nodiscard]] bool drain_live_save();
  std::unique_ptr<Live> live_;
  std::filesystem::path research_root_;
  std::filesystem::path save_path_;
  std::string game_version_;
  NativeCampaignSessionDependencies dependencies_;
  std::thread::id owner_{std::this_thread::get_id()};
  std::unique_ptr<PendingLoad> pending_load_;
  SessionNotice notice_;
  bool save_requested_{};
  bool exit_requested_{};
  bool exit_ready_{};
  bool manual_capture_ready_{};
};

[[nodiscard]] std::filesystem::path default_native_campaign_save_path();

} // namespace stellar::native_map
