#include "native_startup_host.hpp"

#include <utility>

namespace stellar::native_startup_ui {
using namespace stellar::native_setup;
using namespace stellar::native_startup;

NativeStartupHost::NativeStartupHost(
    StartupHostConfig config,
    stellar::native_map::NativeCampaignSessionDependencies dependencies,
    NativeDetachedCampaignGenerator generator)
    : config_(std::move(config)),
      startup_(std::move(dependencies), std::move(generator)) {}

NativeNewCampaignSetupView NativeStartupHost::setup() const {
  return setup_controller_.build();
}
NativeStartupSaveSlots NativeStartupHost::slots() const {
  return list_native_startup_save_slots(config_.default_save_path);
}
StartupHostResult NativeStartupHost::start_new(
    const NativeNewCampaignSetupInput &input) {
  auto assessment = setup_controller_.prepare(input);
  if (!assessment.accepted || !assessment.prepared)
    return {false, std::move(assessment.message)};
  const auto started = startup_.start_new(
      *assessment.prepared, config_.research_root, config_.catalog_path,
      config_.default_save_path, config_.game_version);
  request_id_ = started.request_id;
  return {started.accepted, started.message};
}
StartupHostResult NativeStartupHost::start_load(
    const std::filesystem::path &path) {
  const auto started =
      startup_.start_load(path, config_.research_root, config_.game_version);
  request_id_ = started.request_id;
  return {started.accepted, started.message};
}
void NativeStartupHost::service() { startup_.service(); }
NativeStartupView NativeStartupHost::poll() const { return startup_.poll(); }
bool NativeStartupHost::cancel() {
  return request_id_ != 0 && startup_.cancel(request_id_);
}
std::unique_ptr<stellar::native_map::NativeCampaignSession>
NativeStartupHost::take_ready() {
  if (!request_id_) return {};
  return startup_.take_ready_session(request_id_);
}
} // namespace stellar::native_startup_ui
