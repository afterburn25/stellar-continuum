#include <stellar/core/player_campaign_recovery.hpp>

#include <stellar/engine/save_history.hpp>
#include <stellar/engine/save_integrity.hpp>

#include <algorithm>
#include <fstream>
#include <sstream>
#include <stdexcept>
#include <utility>

namespace stellar::core {
namespace {

[[nodiscard]] bool valid_utf8(std::string_view text) {
  for (std::size_t i = 0; i < text.size();) {
    const auto c = static_cast<unsigned char>(text[i]);
    if (c < 0x80) { ++i; continue; }
    const auto continuation = [&](std::size_t index) {
      return index < text.size() &&
          (static_cast<unsigned char>(text[index]) & 0xc0u) == 0x80u;
    };
    if (c >= 0xc2 && c <= 0xdf && continuation(i + 1)) { i += 2; continue; }
    if (c == 0xe0 && i + 2 < text.size() &&
        static_cast<unsigned char>(text[i + 1]) >= 0xa0 &&
        static_cast<unsigned char>(text[i + 1]) <= 0xbf && continuation(i + 2)) { i += 3; continue; }
    if (c >= 0xe1 && c <= 0xec && continuation(i + 1) && continuation(i + 2)) { i += 3; continue; }
    if (c == 0xed && i + 2 < text.size() &&
        static_cast<unsigned char>(text[i + 1]) >= 0x80 &&
        static_cast<unsigned char>(text[i + 1]) <= 0x9f && continuation(i + 2)) { i += 3; continue; }
    if (c >= 0xee && c <= 0xef && continuation(i + 1) && continuation(i + 2)) { i += 3; continue; }
    if (c == 0xf0 && i + 3 < text.size() &&
        static_cast<unsigned char>(text[i + 1]) >= 0x90 &&
        static_cast<unsigned char>(text[i + 1]) <= 0xbf && continuation(i + 2) && continuation(i + 3)) { i += 4; continue; }
    if (c >= 0xf1 && c <= 0xf3 && continuation(i + 1) && continuation(i + 2) && continuation(i + 3)) { i += 4; continue; }
    if (c == 0xf4 && i + 3 < text.size() &&
        static_cast<unsigned char>(text[i + 1]) >= 0x80 &&
        static_cast<unsigned char>(text[i + 1]) <= 0x8f && continuation(i + 2) && continuation(i + 3)) { i += 4; continue; }
    return false;
  }
  return true;
}

[[nodiscard]] std::string read_utf8_file(const std::filesystem::path &path) {
  std::ifstream input(path, std::ios::binary);
  if (!input)
    throw std::runtime_error("Could not read campaign save file.");
  std::ostringstream buffer;
  buffer << input.rdbuf();
  if (input.bad())
    throw std::runtime_error("Could not finish reading campaign save file.");
  std::string bytes = std::move(buffer).str();
  // Source File.ReadAllText accepts a UTF-8 BOM. Native accepts that exact
  // UTF-8 boundary and rejects UTF-16/other encodings rather than guessing.
  if (bytes.size() >= 3 && static_cast<unsigned char>(bytes[0]) == 0xef &&
      static_cast<unsigned char>(bytes[1]) == 0xbb &&
      static_cast<unsigned char>(bytes[2]) == 0xbf)
    bytes.erase(0, 3);
  if (!valid_utf8(bytes))
    throw std::runtime_error("Campaign save is not strict UTF-8.");
  return bytes;
}

[[nodiscard]] std::string exception_text(const std::exception &error) {
  return error.what();
}

} // namespace

PlayerCampaignLoadError::PlayerCampaignLoadError(
    std::string message, std::vector<PlayerCampaignLoadAttempt> attempts)
    : std::runtime_error(std::move(message)), attempts_(std::move(attempts)) {}

const std::vector<PlayerCampaignLoadAttempt> &
PlayerCampaignLoadError::attempts() const noexcept { return attempts_; }

LoadedPlayerCampaignV17 load_existing_player_campaign_v17(
    const std::filesystem::path &save_path,
    const PlayerCampaignRuntimeFactory &make_runtime,
    const std::function<void(const PlayerCampaignRestorationProgress &)> &progress) {
  const auto &native_path = save_path.native();
  if (native_path.empty() || std::ranges::all_of(native_path, [](const auto value) {
        return value == ' ' || value == '\t' || value == '\r' || value == '\n' ||
               value == '\f' || value == '\v';
      }))
    throw std::invalid_argument("A save path is required.");
  if (!make_runtime)
    throw std::invalid_argument("A fresh research runtime factory is required.");

  double latest = 0.0;
  const auto report = [&](double fraction, std::string status) {
    latest = std::max(latest, fraction);
    if (!progress) return;
    progress({latest, std::move(status)});
  };
  const auto label = [](PlayerCampaignLoadOrigin origin, std::size_t slot) {
    switch (origin) {
    case PlayerCampaignLoadOrigin::Primary:
      return std::string("Primary autosave");
    case PlayerCampaignLoadOrigin::Backup:
      return std::string("Backup autosave");
    case PlayerCampaignLoadOrigin::History:
      return "History autosave (slot " + std::to_string(slot) + ")";
    }
    return std::string("Autosave");
  };
  const auto attempt = [&](const std::filesystem::path &path,
                           PlayerCampaignLoadOrigin origin,
                           std::size_t slot,
                           std::vector<PlayerCampaignLoadAttempt> &failures)
      -> std::optional<LoadedPlayerCampaignV17> {
    std::error_code probe_error;
    if (!std::filesystem::is_regular_file(path, probe_error)) {
      failures.push_back({origin, path, PlayerCampaignLoadAttemptKind::Missing,
          origin == PlayerCampaignLoadOrigin::Primary
              ? "Primary autosave was missing."
              : origin == PlayerCampaignLoadOrigin::Backup
                    ? "No backup autosave was available."
                    : "No " + label(origin, slot) + " was available.",
          {}});
      return std::nullopt;
    }
    try {
      report(.04, "Reading saved campaign");
      // A present-but-mismatched integrity sidecar marks corruption; absent
      // sidecars (older saves) load unchecked.
      if (stellar::engine::verify_integrity(path) ==
          stellar::engine::IntegrityStatus::Mismatch)
        throw std::runtime_error(
            "Campaign save failed its integrity check.");
      auto json = read_utf8_file(path);
      const auto staged = [&](PlayerCampaignJsonStage stage) {
        switch (stage) {
        case PlayerCampaignJsonStage::Parse: report(.16, "Decoding saved campaign"); break;
        case PlayerCampaignJsonStage::DiplomacyDecode: report(.25, "Restoring diplomacy"); break;
        case PlayerCampaignJsonStage::GalaxyDecode: report(.42, "Restoring galaxy state"); break;
        case PlayerCampaignJsonStage::DiplomacyReferences: report(.72, "Validating campaign references"); break;
        case PlayerCampaignJsonStage::ResearchDecode: report(.82, "Restoring research progress"); break;
        case PlayerCampaignJsonStage::DiplomacyRestore: report(.97, "Finalizing restored campaign"); break;
        default: break;
        }
      };
      auto campaign = restore_player_campaign_v17_json(make_runtime(), json, {staged});
      return LoadedPlayerCampaignV17{std::move(campaign), origin, save_path, path,
                                     std::move(failures)};
    } catch (const std::exception &failure) {
      failures.push_back({origin, path, PlayerCampaignLoadAttemptKind::Failed,
          origin == PlayerCampaignLoadOrigin::Primary
              ? "Primary autosave failed:\n" + exception_text(failure)
              : label(origin, slot) +
                    " also failed:\n" + exception_text(failure),
          std::current_exception()});
      return std::nullopt;
    } catch (...) {
      failures.push_back({origin, path, PlayerCampaignLoadAttemptKind::Failed,
          origin == PlayerCampaignLoadOrigin::Primary
              ? "Primary autosave failed:\nUnknown native exception."
              : label(origin, slot) +
                    " also failed:\nUnknown native exception.",
          std::current_exception()});
      return std::nullopt;
    }
  };

  std::vector<PlayerCampaignLoadAttempt> failures;
  if (auto loaded =
          attempt(save_path, PlayerCampaignLoadOrigin::Primary, 0, failures))
    return std::move(*loaded);
  const auto backup_path =
      stellar::engine::history_slot_path(save_path, 1);
  std::error_code backup_probe_error;
  if (std::filesystem::is_regular_file(backup_path, backup_probe_error)) {
    try {
      report(std::max(latest, .12),
             "Primary save unavailable; restoring backup");
      if (auto loaded = attempt(backup_path, PlayerCampaignLoadOrigin::Backup,
                                1, failures))
        return std::move(*loaded);
    } catch (const std::exception &failure) {
      failures.push_back({PlayerCampaignLoadOrigin::Backup, backup_path,
          PlayerCampaignLoadAttemptKind::Failed,
          "Backup autosave also failed:\n" + exception_text(failure),
          std::current_exception()});
    } catch (...) {
      failures.push_back({PlayerCampaignLoadOrigin::Backup, backup_path,
          PlayerCampaignLoadAttemptKind::Failed,
          "Backup autosave also failed:\nUnknown native exception.",
          std::current_exception()});
    }
  } else {
    failures.push_back({PlayerCampaignLoadOrigin::Backup, backup_path,
        PlayerCampaignLoadAttemptKind::Missing,
        "No backup autosave was available.", {}});
  }

  // Rolling history: probe deeper ".bak.<k>" slots that exist, oldest of the
  // surviving chain last. Only existing slots attempt a load — absent slots
  // would just add noise to the failure report.
  for (std::size_t slot = 2;
       slot <= stellar::engine::k_default_save_history_depth; ++slot) {
    const auto history_path =
        stellar::engine::history_slot_path(save_path, slot);
    std::error_code history_probe_error;
    if (!std::filesystem::is_regular_file(history_path, history_probe_error))
      continue;
    report(std::max(latest, .16), "Restoring an older autosave");
    if (auto loaded = attempt(history_path, PlayerCampaignLoadOrigin::History,
                              slot, failures))
      return std::move(*loaded);
  }

  std::ostringstream message;
  for (std::size_t i = 0; i < failures.size(); ++i) {
    if (i) message << '\n';
    message << failures[i].failure;
  }
  throw PlayerCampaignLoadError(message.str(), std::move(failures));
}

} // namespace stellar::core
