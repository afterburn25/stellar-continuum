#include <stellar/core/player_campaign_save.hpp>
#include <stellar/core/developer_campaign_json.hpp>
#include <stellar/core/player_campaign_json.hpp>
#include <stellar/engine/atomic_file_write.hpp>

#include <chrono>
#include <algorithm>
#include <span>
#include <typeinfo>
#include <type_traits>
#include <utility>

namespace stellar::core {
namespace {
void require_save_path(const std::filesystem::path &path) {
  const auto &native = path.native();
  if (native.empty() || std::all_of(native.begin(), native.end(), [](auto c) {
        return c == ' ' || c == '\t' || c == '\r' || c == '\n' || c == '\f' || c == '\v';
      })) throw std::invalid_argument("A save path is required.");
}
}
PreparedPlayerCampaignSave::PreparedPlayerCampaignSave(
    PlayerCampaignPayloadV17Dto payload, const bool developer,
    const bool tools_used)
    : payload_(std::make_shared<const PlayerCampaignPayloadV17Dto>(std::move(payload))),
      developer_(developer), tools_used_(tools_used) {}
PreparedPlayerCampaignSave PreparedPlayerCampaignSave::capture(
    IntegratedAdaptiveCampaignRuntime &runtime, const PlayerCampaignCaptureOptions &options) {
  return PreparedPlayerCampaignSave(capture_player_campaign_v17(runtime, options));
}
PreparedPlayerCampaignSave PreparedPlayerCampaignSave::capture_developer(
    IntegratedAdaptiveCampaignRuntime &runtime, const PlayerCampaignCaptureOptions &options) {
  const auto &provenance = runtime.world().campaign().developer_provenance;
  return PreparedPlayerCampaignSave(
      capture_developer_campaign_v17(runtime, options), true,
      provenance && provenance->tools_used);
}
const PlayerCampaignPayloadV17Dto &PreparedPlayerCampaignSave::payload() const noexcept {
  return *payload_;
}
bool PreparedPlayerCampaignSave::is_developer() const noexcept {
  return developer_;
}
bool PreparedPlayerCampaignSave::tools_used() const noexcept {
  return tools_used_;
}
void write_prepared_player_campaign(const std::filesystem::path &path,
                                   const PreparedPlayerCampaignSave &prepared,
                                   bool preserve_existing_backup) {
  require_save_path(path);
  if (prepared.is_developer())
    throw std::invalid_argument(
        "A Developer-envelope capture requires the Developer writer.");
  const auto json = encode_player_campaign_v17_json(prepared.payload());
  stellar::engine::write_file_atomically(path,
      std::as_bytes(std::span(json.data(), json.size())), {preserve_existing_backup});
}

PlayerCampaignSaveController::PlayerCampaignSaveController(
    CampaignAutosavePolicy policy, PlayerCampaignPreparedWriter writer,
    const bool developer_mode)
    : scheduler_(policy), writer_(std::move(writer)),
      developer_mode_(developer_mode) {
  if (!writer_) throw std::invalid_argument("A prepared campaign writer is required.");
}
PlayerCampaignSaveController::~PlayerCampaignSaveController() {
  // Explicit exit/replacement drains report failures before destruction. The
  // final owner still waits, so no detached job can outlive its payload.
  jobs_.wait_idle();
}
void PlayerCampaignSaveController::require_owner() const {
  if (std::this_thread::get_id() != owner_)
    throw std::logic_error("Campaign saves must be controlled by the simulation owner thread.");
}
void PlayerCampaignSaveController::configure(std::filesystem::path path,
    std::uint64_t revision, double day, bool recovered_from_backup) {
  require_owner();
  if (pending_) throw std::logic_error("Drain the pending campaign save before replacing its session.");
  require_save_path(path);
  scheduler_.reset(day);
  path_ = std::move(path); revision_ = revision;
  preserve_backup_ = recovered_from_backup; configured_ = true;
}
PreparedPlayerCampaignSave PlayerCampaignSaveController::capture_prepared(
    IntegratedAdaptiveCampaignRuntime &runtime,
    const PlayerCampaignCaptureOptions &options) const {
  return developer_mode_
             ? PreparedPlayerCampaignSave::capture_developer(runtime, options)
             : PreparedPlayerCampaignSave::capture(runtime, options);
}
PlayerCampaignSaveResult PlayerCampaignSaveController::failure(
    std::exception_ptr exception, double day, const std::filesystem::path &path,
    bool preserve) const {
  PlayerCampaignSaveResult result{false, path, day, preserve, {}, {}};
  try { std::rethrow_exception(exception); }
  catch (const std::exception &error) {
    result.error_type = typeid(error).name(); result.error_message = error.what();
  } catch (...) {
    result.error_type = "unknown"; result.error_message = "Unknown campaign save failure.";
  }
  return result;
}
std::optional<PlayerCampaignSaveResult> PlayerCampaignSaveController::complete(
    double current_day, const std::filesystem::path &current_path,
    std::uint64_t current_revision, bool wait) {
  require_owner();
  if (!pending_ || (!wait && pending_->task.wait_for(std::chrono::seconds(0)) != std::future_status::ready))
    return std::nullopt;
  // Validate scheduling time before consuming the future.
  (void)scheduler_.is_due(current_day);
  auto record = std::move(*pending_); pending_.reset();
  try {
    record.task.get();
    if (record.revision != current_revision || record.path.native() != current_path.native())
      throw std::logic_error("A scheduled autosave completed after its campaign session was replaced.");
    scheduler_.mark_success(current_day); preserve_backup_ = false;
    return PlayerCampaignSaveResult{true, record.path, record.captured_day, record.preserved_backup, {}, {}};
  } catch (...) {
    auto result = failure(std::current_exception(), record.captured_day, record.path, record.preserved_backup);
    scheduler_.mark_failure(current_day); return result;
  }
}
std::optional<PlayerCampaignSaveResult> PlayerCampaignSaveController::after_frame(
    CampaignFrame &frame, const CampaignFrameResult &result,
    const std::string &game_version, const std::string &saved_at_utc) {
  require_owner();
  if (!configured_) throw std::logic_error("Configure the campaign save session first.");
  if (result.route != CampaignFrameRoute::Strategic || !result.ready_for_save_capture)
    return std::nullopt;
  const auto day = frame.clock().simulation_days();
  auto completed = complete(day, path_, revision_);
  if (pending_ || !scheduler_.is_due(day)) return completed;
  const bool preserve = preserve_backup_;
  try {
    auto prepared = capture_prepared(frame.runtime(), {day, game_version, saved_at_utc});
    const auto destination = path_; const auto writer = writer_;
    // Allocate all pending metadata before submitting. No operation after
    // submit can throw and lose tracking of an already running write.
    Pending record{{}, destination, revision_, day, preserve};
    static_assert(std::is_nothrow_move_constructible_v<Pending>);
    record.task = jobs_.submit([prepared = std::move(prepared), destination, preserve, writer] {
      writer(destination, prepared, preserve);
    });
    pending_.emplace(std::move(record));
  } catch (...) {
    auto failed = failure(std::current_exception(), day, path_, preserve);
    scheduler_.mark_failure(day); return failed;
  }
  return completed;
}
PlayerCampaignSaveResult PlayerCampaignSaveController::save_manual(
    IntegratedAdaptiveCampaignRuntime &runtime, const PlayerCampaignCaptureOptions &options) {
  require_owner();
  if (!configured_) throw std::logic_error("Configure the campaign save session first.");
  if (pending_) throw std::logic_error("Consume and report the pending autosave before a manual save.");
  const bool preserve = preserve_backup_;
  try {
    const auto prepared = capture_prepared(runtime, options);
    writer_(path_, prepared, preserve);
    scheduler_.mark_success(options.simulation_days); preserve_backup_ = false;
    return {true, path_, options.simulation_days, preserve, {}, {}};
  } catch (...) {
    auto result = failure(std::current_exception(), options.simulation_days, path_, preserve);
    scheduler_.mark_failure(options.simulation_days); return result;
  }
}
bool PlayerCampaignSaveController::pending() const noexcept { return pending_.has_value(); }
bool PlayerCampaignSaveController::preserves_recovered_backup() const noexcept { return preserve_backup_; }
double PlayerCampaignSaveController::next_due_day() const noexcept { return scheduler_.next_due_day(); }
const std::filesystem::path &PlayerCampaignSaveController::path() const noexcept { return path_; }
std::uint64_t PlayerCampaignSaveController::revision() const noexcept { return revision_; }
} // namespace stellar::core
