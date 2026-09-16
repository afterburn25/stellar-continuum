// Developer-mode coverage for the native campaign session boundary:
// provenance-selected Developer frame/save policy, the developer envelope
// on every write path, the command dispatcher and the developer loader
// used for in-session reloads.

#include "native_campaign_session.hpp"

#include <stellar/core/developer_campaign_save.hpp>
#include <stellar/core/developer_campaign_session.hpp>
#include <stellar/core/galaxy_catalog.hpp>
#include <stellar/core/integrated_adaptive_campaign.hpp>
#include <stellar/core/player_campaign_recovery.hpp>

#include <chrono>
#include <filesystem>
#include <iostream>
#include <stdexcept>
#include <string>
#include <thread>

namespace fs = std::filesystem;
using namespace stellar::core;
using namespace stellar::native_map;

namespace {

int failures{0};
void check(const bool condition, const char *message) {
  if (!condition) {
    ++failures;
    std::cerr << "FAIL: " << message << '\n';
  }
}

constexpr const char *kTimestamp = "2026-09-16T00:00:00Z";

[[nodiscard]] PlayerCampaignLoadOrigin source_origin(
    const DeveloperCampaignSource source) noexcept {
  switch (source) {
    case DeveloperCampaignSource::RecoveredFromBackup:
    case DeveloperCampaignSource::ImportedLegacyDemoBackup:
      return PlayerCampaignLoadOrigin::Backup;
    default:
      return PlayerCampaignLoadOrigin::Primary;
  }
}

[[nodiscard]] NativeCampaignLoader developer_loader() {
  return [](const fs::path &path, const PlayerCampaignRuntimeFactory &factory,
            const std::function<void(const PlayerCampaignRestorationProgress &)>
                &progress) {
    auto bootstrap = load_existing_developer_campaign(path, factory, progress);
    if (!bootstrap.loaded)
      throw PlayerCampaignLoadError("No Developer campaign save is available.",
                                    std::move(bootstrap.prior_attempts));
    return LoadedPlayerCampaignV17{std::move(*bootstrap.loaded),
                                   source_origin(bootstrap.source),
                                   std::move(bootstrap.requested_path),
                                   std::move(bootstrap.loaded_path),
                                   std::move(bootstrap.prior_attempts)};
  };
}

[[nodiscard]] NativeCampaignSessionDependencies developer_dependencies() {
  NativeCampaignSessionDependencies dependencies;
  dependencies.save_writer = write_prepared_developer_campaign;
  dependencies.loader = developer_loader();
  return dependencies;
}

bool wait_for_load(NativeCampaignSession &session) {
  const auto deadline =
      std::chrono::steady_clock::now() + std::chrono::seconds(60);
  while (session.load_pending() &&
         std::chrono::steady_clock::now() < deadline) {
    if (session.service(kTimestamp, false)) return true;
    std::this_thread::sleep_for(std::chrono::milliseconds(2));
  }
  return false;
}

void run(const fs::path &catalog_path, const std::string &research_root,
         const fs::path &root) {
  const auto catalog_storage = load_nearby_catalog(catalog_path);
  const std::span<const CatalogStar> catalog = catalog_storage;
  const PersistableFreshCampaignOptions options{kTimestamp, 250, 2, 0};
  const PlayerCampaignRuntimeFactory make_runtime = [&research_root] {
    return load_adaptive_research_strategic_runtime(research_root);
  };
  const auto save = root / std::string(developer_save_file_name);

  auto world = create_developer_campaign(playable_demo_seed, catalog, options);
  auto session = NativeCampaignSession::create_fresh(
      IntegratedAdaptiveCampaignRuntime::create_fresh(make_runtime(),
                                                    std::move(world)),
      research_root, save, "0.1.7-alpha", developer_dependencies());

  check(session->developer_mode() && !session->developer_tools_used(),
        "fresh Developer session reports Developer mode without tools use");

  // The mode-switch checkpoint boundary must not wait for a frame.
  check(session->checkpoint_now(kTimestamp),
        "checkpoint_now writes the Developer envelope before any frame");
  {
    auto back = load_existing_developer_campaign(save, make_runtime);
    check(back.loaded && back.loaded->galaxy().developer_provenance &&
              !back.loaded->galaxy().developer_provenance->tools_used,
          "checkpoint envelope loads with unused Developer provenance");
  }
  {
    bool rejected = false;
    try {
      (void)load_existing_player_campaign_v17(save, make_runtime);
    } catch (...) {
      rejected = true;
    }
    check(rejected, "the player loader rejects the Developer envelope");
  }

  auto outcome = session->run_developer_command("grant_resources", kTimestamp);
  check(outcome.accepted && session->developer_tools_used(),
        "grant_resources is accepted and marks the campaign Tools used");
  {
    auto back = load_existing_developer_campaign(save, make_runtime);
    check(back.loaded && back.loaded->galaxy().developer_provenance &&
              back.loaded->galaxy().developer_provenance->tools_used,
          "the post-command checkpoint persists Tools used");
  }

  const auto day = session->frame().clock().simulation_days();
  outcome = session->run_developer_command("advance_30_days", kTimestamp);
  check(outcome.accepted &&
            session->frame().clock().simulation_days() >= day + 30.,
        "advance_30_days runs through the canonical session simulation");

  outcome = session->run_developer_command("not_a_command", kTimestamp);
  check(!outcome.accepted,
        "unknown Developer command ids are rejected");

  // In-session Load must round-trip through the Developer loader.
  session->frame().clock().restore(day);
  session->request_load();
  check(wait_for_load(*session) && session->developer_mode() &&
            session->developer_tools_used() &&
            session->frame().clock().simulation_days() >= day + 30. &&
            session->frame().clock().speed() == StrategicSpeed::Paused,
        "Developer sessions reload through the Developer envelope");

  // A Player session never exposes Developer commands or the Developer
  // envelope, even when sharing a directory.
  const auto player_save = root / "campaign.player17.json";
  auto player_world =
      seed_persistable_fresh_campaign(4242, catalog, options);
  auto player_session = NativeCampaignSession::create_fresh(
      IntegratedAdaptiveCampaignRuntime::create_fresh(make_runtime(),
                                                    std::move(player_world)),
      research_root, player_save, "0.1.7-alpha");
  check(!player_session->developer_mode(),
        "player sessions do not report Developer mode");
  outcome =
      player_session->run_developer_command("grant_resources", kTimestamp);
  check(!outcome.accepted,
        "player sessions reject Developer commands");
  check(player_session->checkpoint_now(kTimestamp),
        "player checkpoint writes the ordinary envelope");
  {
    bool rejected = false;
    try {
      (void)load_existing_developer_campaign(player_save, make_runtime);
    } catch (...) {
      rejected = true;
    }
    check(rejected,
          "the Developer loader rejects the ordinary player envelope");
  }
}

} // namespace

int main(int argc, char **argv) {
  try {
    if (argc != 4) {
      std::cerr << "usage: native_developer_session_tests <catalog.json> "
                   "<research_root> <scratch>\n";
      return 2;
    }
    const auto root =
        fs::absolute(argv[3]) / "native-developer-session";
    std::error_code error;
    fs::remove_all(root, error);
    fs::create_directories(root);
    run(fs::absolute(argv[1]), argv[2], root);
    fs::remove_all(root, error);
    if (failures != 0) {
      std::cerr << failures << " native developer session checks failed\n";
      return 1;
    }
    std::cout << "native developer session checks passed\n";
    return 0;
  } catch (const std::exception &error) {
    std::cerr << "unexpected exception: " << error.what() << '\n';
    return 2;
  }
}
