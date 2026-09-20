#include <stellar/core/developer_campaign_session.hpp>

#include <stellar/engine/save_history.hpp>
#include <stellar/engine/save_integrity.hpp>

#include <algorithm>
#include <cctype>
#include <fstream>
#include <sstream>
#include <stdexcept>
#include <utility>

namespace stellar::core {
namespace {

using LoadProgress =
    std::function<void(const PlayerCampaignRestorationProgress &)>;

void require_save_path(const std::filesystem::path &path) {
  const auto &native = path.native();
  if (native.empty() ||
      std::all_of(native.begin(), native.end(), [](auto c) {
        return c == ' ' || c == '\t' || c == '\r' || c == '\n' || c == '\f' ||
               c == '\v';
      }))
    throw std::invalid_argument("A save path is required.");
}

[[nodiscard]] std::filesystem::path backup_of(
    const std::filesystem::path &path) {
  auto backup = path;
  backup += ".bak";
  return backup;
}

[[nodiscard]] bool is_regular(const std::filesystem::path &path) {
  std::error_code error;
  return std::filesystem::is_regular_file(path, error);
}

[[nodiscard]] std::string read_utf8_file(const std::filesystem::path &path) {
  std::ifstream input(path, std::ios::binary);
  if (!input)
    throw std::runtime_error("Could not read campaign save file.");
  std::ostringstream buffer;
  buffer << input.rdbuf();
  if (input.bad())
    throw std::runtime_error("Could not finish reading campaign save file.");
  auto bytes = std::move(buffer).str();
  if (bytes.size() >= 3 && static_cast<unsigned char>(bytes[0]) == 0xef &&
      static_cast<unsigned char>(bytes[1]) == 0xbb &&
      static_cast<unsigned char>(bytes[2]) == 0xbf)
    bytes.erase(0, 3);
  return bytes;
}

using DeveloperLoader = std::function<RestoredPlayerCampaignV17(
    std::string_view json, const PlayerCampaignJsonRestoreHooks &)>;

// One bounded attempt at a single candidate path. Each attempt consumes a
// fresh runtime from the factory.
[[nodiscard]] std::optional<DeveloperCampaignBootstrap> attempt_load(
    const std::filesystem::path &candidate, const std::filesystem::path &requested,
    PlayerCampaignLoadOrigin origin, DeveloperCampaignSource success,
    bool importing_legacy, const DeveloperLoader &loader,
    std::vector<PlayerCampaignLoadAttempt> &failures,
    const LoadProgress &progress) {
  const auto kind = importing_legacy ? "legacy demo" : "Developer";
  const auto slot_label = [&](const bool capitalized) {
    const auto noun = origin == PlayerCampaignLoadOrigin::History
                          ? "history " + std::string(kind) + " save"
                          : "backup " + std::string(kind) + " save";
    return capitalized ? std::string(1, static_cast<char>(
                                           std::toupper(noun.front()))) +
                             noun.substr(1)
                       : noun;
  };
  if (!is_regular(candidate)) {
    failures.push_back(
        {origin, candidate, PlayerCampaignLoadAttemptKind::Missing,
         origin == PlayerCampaignLoadOrigin::Primary
             ? "Primary " + std::string(kind) + " save was missing."
             : "No " + slot_label(false) + " was available.",
         {}});
    return std::nullopt;
  }
  try {
    if (progress)
      progress({.04, "Reading saved campaign"});
    const auto json = read_utf8_file(candidate);
    auto campaign = loader(json, {});
    if (importing_legacy)
      campaign.galaxy().developer_provenance =
          CampaignDeveloperProvenance{false};
    DeveloperCampaignBootstrap result;
    result.loaded = std::move(campaign);
    result.source = success;
    result.requested_path = requested;
    result.loaded_path = candidate;
    result.prior_attempts = std::move(failures);
    if (!result.prior_attempts.empty() || importing_legacy) {
      for (const auto &attempt : result.prior_attempts) {
        if (!result.load_failure.empty())
          result.load_failure += '\n';
        result.load_failure += attempt.failure;
      }
      if (importing_legacy) {
        if (!result.load_failure.empty())
          result.load_failure += '\n';
        result.load_failure +=
            "Imported the legacy demo into Developer mode in memory; its "
            "original save files remain unchanged.";
      }
    }
    return result;
  } catch (const std::exception &failure) {
    failures.push_back(
        {origin, candidate, PlayerCampaignLoadAttemptKind::Failed,
         (origin == PlayerCampaignLoadOrigin::Primary
              ? "Primary " + std::string(kind) + " save failed:\n"
              : slot_label(true) + " also failed:\n") +
             failure.what(),
         std::current_exception()});
    return std::nullopt;
  } catch (...) {
    failures.push_back(
        {origin, candidate, PlayerCampaignLoadAttemptKind::Failed,
         (origin == PlayerCampaignLoadOrigin::Primary
              ? "Primary " + std::string(kind) + " save failed:\n"
              : slot_label(true) + " also failed:\n") +
             std::string("Unknown native exception."),
         std::current_exception()});
    return std::nullopt;
  }
}

[[nodiscard]] std::optional<DeveloperCampaignBootstrap> load_pair(
    const std::filesystem::path &primary, const std::filesystem::path &requested,
    bool importing_legacy, const DeveloperLoader &loader,
    std::vector<PlayerCampaignLoadAttempt> &failures,
    const LoadProgress &progress) {
  const auto backup = backup_of(primary);
  if (auto loaded = attempt_load(
          primary, requested, PlayerCampaignLoadOrigin::Primary,
          importing_legacy ? DeveloperCampaignSource::ImportedLegacyDemo
                           : DeveloperCampaignSource::LoadedSave,
          importing_legacy, loader, failures, progress))
    return loaded;
  if (auto loaded = attempt_load(
          backup, requested, PlayerCampaignLoadOrigin::Backup,
          importing_legacy ? DeveloperCampaignSource::ImportedLegacyDemoBackup
                           : DeveloperCampaignSource::RecoveredFromBackup,
          importing_legacy, loader, failures, progress))
    return loaded;
  // Rolling history slots (.bak.2 .. .bak.N) — only existing slots attempt.
  for (std::size_t slot = 2;
       slot <= stellar::engine::k_default_save_history_depth; ++slot) {
    const auto history =
        stellar::engine::history_slot_path(primary, slot);
    if (!is_regular(history)) continue;
    if (auto loaded = attempt_load(
            history, requested, PlayerCampaignLoadOrigin::History,
            importing_legacy
                ? DeveloperCampaignSource::ImportedLegacyDemoBackup
                : DeveloperCampaignSource::RecoveredFromBackup,
            importing_legacy, loader, failures, progress))
      return loaded;
  }
  return std::nullopt;
}

[[nodiscard]] DeveloperLoader developer_loader(
    const PlayerCampaignRuntimeFactory &make_runtime) {
  return [&make_runtime](const std::string_view json,
                         const PlayerCampaignJsonRestoreHooks &hooks) {
    return restore_developer_campaign_json(make_runtime(), json, hooks)
        .campaign;
  };
}

[[nodiscard]] DeveloperLoader legacy_loader(
    const PlayerCampaignRuntimeFactory &make_runtime) {
  return [&make_runtime](const std::string_view json,
                         const PlayerCampaignJsonRestoreHooks &hooks) {
    return restore_player_campaign_v17_json(make_runtime(), json, hooks);
  };
}

[[nodiscard]] std::string join_failures(
    const std::vector<PlayerCampaignLoadAttempt> &failures) {
  std::ostringstream message;
  for (std::size_t i = 0; i < failures.size(); ++i) {
    if (i)
      message << '\n';
    message << failures[i].failure;
  }
  return message.str();
}

[[nodiscard]] DeveloperCampaignBootstrap create_result(
    const std::filesystem::path &requested, const std::int64_t seed,
    const std::span<const CatalogStar> catalog,
    const PersistableFreshCampaignOptions &options,
    std::vector<PlayerCampaignLoadAttempt> failures) {
  DeveloperCampaignBootstrap result;
  result.fresh = create_developer_campaign(seed, catalog, options);
  result.source = failures.empty()
                      ? DeveloperCampaignSource::Created
                      : DeveloperCampaignSource::RecoveredFromInvalidSave;
  result.requested_path = requested;
  result.prior_attempts = std::move(failures);
  result.load_failure = join_failures(result.prior_attempts);
  return result;
}

} // namespace

std::filesystem::path
developer_save_path_beside(const std::filesystem::path &player_save_path) {
  const auto directory = player_save_path.parent_path();
  if (directory.empty())
    throw std::invalid_argument("Save path needs a parent directory.");
  return directory / std::string(developer_save_file_name);
}

FreshCampaignState create_developer_campaign(
    const std::int64_t seed, const std::span<const CatalogStar> catalog,
    const PersistableFreshCampaignOptions &options) {
  auto world = seed_persistable_fresh_campaign(seed, catalog, options);
  world.developer_provenance = CampaignDeveloperProvenance{false};
  return world;
}

DeveloperCampaignBootstrap load_existing_developer_campaign(
    const std::filesystem::path &save_path,
    const PlayerCampaignRuntimeFactory &make_runtime,
    const LoadProgress &progress) {
  require_save_path(save_path);
  if (!make_runtime)
    throw std::invalid_argument("A fresh research runtime factory is required.");

  std::vector<PlayerCampaignLoadAttempt> failures;
  const auto backup = backup_of(save_path);
  if (is_regular(save_path) || is_regular(backup) ||
      stellar::engine::has_save_history(save_path))
    if (auto loaded =
            load_pair(save_path, save_path, false,
                      developer_loader(make_runtime), failures, progress))
      return std::move(*loaded);

  const auto legacy = save_path.parent_path() /
                      std::string(legacy_demo_save_file_name);
  if (is_regular(legacy) || is_regular(backup_of(legacy)) ||
      stellar::engine::has_save_history(legacy))
    if (auto loaded =
            load_pair(legacy, save_path, true, legacy_loader(make_runtime),
                      failures, progress))
      return std::move(*loaded);

  if (failures.empty())
    throw PlayerCampaignLoadError(
        "No Developer campaign save or legacy demo save is available.", {});
  throw PlayerCampaignLoadError(join_failures(failures), std::move(failures));
}

DeveloperCampaignBootstrap load_or_create_developer_campaign(
    const std::filesystem::path &save_path, const std::int64_t fallback_seed,
    const std::span<const CatalogStar> catalog,
    const PersistableFreshCampaignOptions &options,
    const PlayerCampaignRuntimeFactory &make_runtime,
    const LoadProgress &progress) {
  require_save_path(save_path);
  if (!make_runtime)
    throw std::invalid_argument("A fresh research runtime factory is required.");

  std::vector<PlayerCampaignLoadAttempt> failures;
  const auto backup = backup_of(save_path);
  if (is_regular(save_path) || is_regular(backup) ||
      stellar::engine::has_save_history(save_path)) {
    if (auto loaded =
            load_pair(save_path, save_path, false,
                      developer_loader(make_runtime), failures, progress))
      return std::move(*loaded);
    return create_result(save_path, fallback_seed, catalog, options,
                         std::move(failures));
  }

  const auto legacy = save_path.parent_path() /
                      std::string(legacy_demo_save_file_name);
  if (is_regular(legacy) || is_regular(backup_of(legacy)) ||
      stellar::engine::has_save_history(legacy)) {
    if (auto loaded =
            load_pair(legacy, save_path, true, legacy_loader(make_runtime),
                      failures, progress))
      return std::move(*loaded);
    return create_result(save_path, fallback_seed, catalog, options,
                         std::move(failures));
  }

  return create_result(save_path, fallback_seed, catalog, options,
                       std::move(failures));
}

} // namespace stellar::core
