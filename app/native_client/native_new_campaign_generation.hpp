#pragma once

#include "native_new_campaign_setup.hpp"

#include <stellar/core/adaptive_research_strategic_runtime.hpp>
#include <stellar/core/fresh_campaign.hpp>
#include <stellar/core/integrated_adaptive_campaign.hpp>

#include <cstdint>
#include <filesystem>
#include <functional>
#include <memory>
#include <optional>
#include <string>

namespace stellar::native_setup {

enum class NativeNewCampaignGenerationPhase {
  Idle,
  LoadingCatalog,
  LoadingResearchDefinitions,
  SeedingCampaign,
  Ready,
  Failed,
  Cancelling,
  Cancelled,
  Consumed,
};

struct NativeNewCampaignGenerationView {
  std::uint64_t request_id{}, revision{};
  NativeNewCampaignGenerationPhase phase{NativeNewCampaignGenerationPhase::Idle};
  std::string status;
  bool worker_running{}, ready{};
};

struct NativeNewCampaignGenerationStart {
  bool accepted{};
  std::uint64_t request_id{};
  std::string message;
};

// Both members are detached owning values. Neither constructs a lane network,
// CampaignFrame, NativeCampaignSession, or any other thread-owned host.
struct NativeDetachedNewCampaign {
  stellar::core::FreshCampaignState world;
  stellar::core::AdaptiveResearchStrategicRuntime research;
  stellar::core::DeveloperResearchOptions developer_research;
};

using NativeNewCampaignPhaseSink =
    std::function<void(NativeNewCampaignGenerationPhase)>;
using NativeDetachedCampaignGenerator = std::function<NativeDetachedNewCampaign(
    const NativePreparedNewCampaign &, const std::filesystem::path &research_root,
    const std::filesystem::path &catalog_path,
    const NativeNewCampaignPhaseSink &)>;

[[nodiscard]] NativeDetachedNewCampaign generate_detached_new_campaign(
    const NativePreparedNewCampaign &, const std::filesystem::path &research_root,
    const std::filesystem::path &catalog_path,
    const NativeNewCampaignPhaseSink &);

class NativeNewCampaignGenerationController final {
public:
  NativeNewCampaignGenerationController();
  explicit NativeNewCampaignGenerationController(
      NativeDetachedCampaignGenerator);
  ~NativeNewCampaignGenerationController();
  NativeNewCampaignGenerationController(
      const NativeNewCampaignGenerationController &) = delete;
  NativeNewCampaignGenerationController &
  operator=(const NativeNewCampaignGenerationController &) = delete;
  NativeNewCampaignGenerationController(NativeNewCampaignGenerationController &&) =
      delete;
  NativeNewCampaignGenerationController &
  operator=(NativeNewCampaignGenerationController &&) = delete;

  [[nodiscard]] NativeNewCampaignGenerationStart start(
      const NativePreparedNewCampaign &, std::filesystem::path research_root,
      std::filesystem::path catalog_path);
  [[nodiscard]] NativeNewCampaignGenerationView poll() const;
  [[nodiscard]] bool cancel(std::uint64_t request_id);
  [[nodiscard]] bool discard(std::uint64_t request_id);
  [[nodiscard]] std::optional<NativeDetachedNewCampaign>
  take_ready(std::uint64_t request_id);
  // This method must run on the consuming owner thread. The integrated runtime
  // and its lane/coordinator graph are first constructed here. A Core
  // construction exception records a Failed view and is rethrown.
  [[nodiscard]] std::optional<stellar::core::IntegratedAdaptiveCampaignRuntime>
  activate_ready(std::uint64_t request_id);

private:
  struct Storage;
  std::unique_ptr<Storage> storage_;
};

} // namespace stellar::native_setup
