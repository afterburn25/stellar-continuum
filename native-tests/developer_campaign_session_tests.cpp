#include <stellar/core/developer_campaign_save.hpp>
#include <stellar/core/developer_campaign_session.hpp>
#include <stellar/core/galaxy_catalog.hpp>
#include <stellar/core/integrated_adaptive_campaign.hpp>
#include <stellar/core/player_campaign_save.hpp>

#include <exception>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>

using namespace stellar::core;
namespace fs = std::filesystem;

namespace {
int failures{0};
void check(const bool condition, const char *message) {
  if (!condition) {
    ++failures;
    std::cerr << "FAIL: " << message << '\n';
  }
}

std::string research_root;
std::span<const CatalogStar> catalog;

PlayerCampaignRuntimeFactory make_runtime() {
  return [] {
    return load_adaptive_research_strategic_runtime(research_root);
  };
}

const PersistableFreshCampaignOptions options{"2026-09-16T00:00:00Z", 250, 2,
                                              0};

void write_developer_save(const fs::path &path, const bool tools_used) {
  auto world = create_developer_campaign(4242, catalog, options);
  world.developer_provenance->tools_used = tools_used;
  auto runtime = IntegratedAdaptiveCampaignRuntime::create_fresh(
      load_adaptive_research_strategic_runtime(research_root),
      std::move(world));
  write_prepared_developer_campaign(
      path,
      PreparedDeveloperCampaignSave::capture(runtime,
                                             {1.5, "test", "2026-09-16T00:00:00Z"}),
      false);
}

void write_player_save(const fs::path &path) {
  auto world = seed_persistable_fresh_campaign(4242, catalog, options);
  auto runtime = IntegratedAdaptiveCampaignRuntime::create_fresh(
      load_adaptive_research_strategic_runtime(research_root),
      std::move(world));
  write_prepared_player_campaign(
      path,
      PreparedPlayerCampaignSave::capture(runtime,
                                          {2.5, "test", "2026-09-16T00:00:00Z"}),
      false);
}
} // namespace

int run(int argc, char **argv) {
  if (argc != 3) {
    std::cerr << "usage: developer_campaign_session_tests <catalog.json> "
                 "<research_root>\n";
    return 2;
  }
  research_root = argv[2];
  const auto stars = load_nearby_catalog(argv[1]);
  catalog = stars;

  const auto root =
      fs::temp_directory_path() / "stellar-developer-session-test";
  std::error_code error;
  fs::remove_all(root, error);
  fs::create_directories(root);
  const auto save = root / std::string(developer_save_file_name);

  check(developer_save_path_beside(root / "player-autosave.json") == save,
        "developer save path sits beside the player save");

  auto boot = load_or_create_developer_campaign(
      save, 777, catalog, options, make_runtime());
  check(boot.source == DeveloperCampaignSource::Created && boot.fresh &&
            boot.fresh->developer_provenance.has_value() &&
            !boot.fresh->developer_provenance->tools_used,
        "load_or_create on an empty slot creates a provenance-stamped world");

  bool threw = false;
  try {
    (void)load_existing_developer_campaign(save, make_runtime());
  } catch (const PlayerCampaignLoadError &) {
    threw = true;
  }
  check(threw, "load_existing throws when no Developer or demo save exists");

  write_developer_save(save, false);
  boot = load_or_create_developer_campaign(save, 777, catalog, options,
                                           make_runtime());
  check(boot.source == DeveloperCampaignSource::LoadedSave && boot.loaded &&
            boot.loaded->galaxy().developer_provenance.has_value(),
        "load_or_create loads the Developer primary save");
  check(boot.loaded->galaxy().player_civilization_id ==
            boot.loaded->galaxy().civilizations.front().id ||
            boot.loaded->galaxy().systems.size() == 250,
        "loaded Developer campaign preserves world state");

  write_developer_save(save, false);
  const auto backup = fs::path(save.string() + ".bak");
  fs::copy_file(save, backup, error);
  {
    std::ofstream garbage(save);
    garbage << "{ not json";
  }
  boot = load_or_create_developer_campaign(save, 777, catalog, options,
                                           make_runtime());
  check(boot.source == DeveloperCampaignSource::RecoveredFromBackup &&
            boot.loaded,
        "load_or_create recovers the .bak twin when the primary is corrupt");
  check(!boot.load_failure.empty(),
        "backup recovery reports the primary failure");

  {
    std::ofstream garbage(backup);
    garbage << "{ also broken";
  }
  boot = load_or_create_developer_campaign(save, 777, catalog, options,
                                           make_runtime());
  check(boot.source == DeveloperCampaignSource::RecoveredFromInvalidSave &&
            boot.fresh && boot.fresh->developer_provenance.has_value(),
        "load_or_create creates a fresh world when both saves are invalid");
  check(boot.fresh->generation_metadata.has_value(),
        "recovery creates a persistable world, not a bare seed");

  fs::remove_all(root, error);
  fs::create_directories(root);
  write_player_save(root / std::string(legacy_demo_save_file_name));
  boot = load_or_create_developer_campaign(save, 777, catalog, options,
                                           make_runtime());
  check(boot.source == DeveloperCampaignSource::ImportedLegacyDemo &&
            boot.loaded &&
            boot.loaded->galaxy().developer_provenance.has_value() &&
            !boot.loaded->galaxy().developer_provenance->tools_used,
        "load_or_create imports the legacy demo save as a Developer campaign");
  check(boot.loaded_path.filename() == legacy_demo_save_file_name,
        "legacy import records the demo path it loaded");

  write_developer_save(save, true);
  boot = load_or_create_developer_campaign(save, 777, catalog, options,
                                           make_runtime());
  check(boot.source == DeveloperCampaignSource::LoadedSave &&
            boot.loaded->galaxy().developer_provenance->tools_used,
        "an existing Developer save wins over the legacy demo import");

  fs::remove_all(root, error);
  if (failures != 0) {
    std::cerr << failures << " developer session checks failed\n";
    return 1;
  }
  std::cout << "developer session checks passed\n";
  return 0;
}

int main(int argc, char **argv) {
  try {
    return run(argc, argv);
  } catch (const std::exception &error) {
    std::cerr << "unexpected exception: " << error.what() << '\n';
    return 2;
  }
}
