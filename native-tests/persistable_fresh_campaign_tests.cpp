#include <stellar/core/persistable_fresh_campaign.hpp>
#include <stellar/core/detail/adaptive_research_sha256.hpp>

#define main maintained_fresh_campaign_test_main
#include "fresh_campaign_tests.cpp"
#undef main

#include <cctype>
#include <filesystem>
#include <fstream>
#include <span>
#include <typeinfo>

namespace {

constexpr auto fixture_sha =
    "38C23A52A8D0D8AB4DFFE8CF40110CE1B3305238FB59C87E9FE8450C9602C9B9";

std::string bytes(const std::filesystem::path &path) {
  std::ifstream input(path, std::ios::binary);
  check(input.good(), "Cannot open '" + path.string() + "'.");
  return {std::istreambuf_iterator<char>(input), {}};
}

std::string sha256(std::span<const unsigned char> value) {
  const auto digest = stellar::core::detail::adaptive_research_sha256(value);
  constexpr char digits[] = "0123456789ABCDEF";
  std::string result;
  result.reserve(64);
  for (const auto byte : digest) {
    result.push_back(digits[byte >> 4]);
    result.push_back(digits[byte & 15]);
  }
  return result;
}

std::string sha256(const std::string &value) {
  return sha256({reinterpret_cast<const unsigned char *>(value.data()),
                 value.size()});
}

void equal_named_core(const GalacticCoreMetadata &actual,
                      const Json &expected, const std::string &field) {
  check(actual.landmark_key == expected.at("LandmarkKey").get<std::string>(),
        field + ".LandmarkKey");
  equal_number(actual.x, number(expected.at("X")), field + ".X");
  equal_number(actual.y, number(expected.at("Y")), field + ".Y");
  equal_number(actual.exclusion_radius, number(expected.at("ExclusionRadius")),
               field + ".ExclusionRadius");
}

void equal_metadata(const GalaxyGenerationMetadata &actual,
                    const Json &expected, const std::string &field) {
  check(actual.entered_seed == expected.at("EnteredSeed").get<std::string>(),
        field + ".EnteredSeed");
  check(actual.internal_seed == expected.at("InternalSeed").get<std::int64_t>(),
        field + ".InternalSeed");
  check(actual.generator_version ==
            expected.at("GeneratorVersion").get<std::string>(),
        field + ".GeneratorVersion");
  check(actual.created_at_utc == expected.at("CreatedAtUtc").get<std::string>(),
        field + ".CreatedAtUtc");
  check(actual.system_count == expected.at("SystemCount").get<int>(),
        field + ".SystemCount");
  check(actual.galaxy_shape == expected.at("GalaxyShape").get<std::string>(),
        field + ".GalaxyShape");
  check(actual.stellar_variety ==
            expected.at("StellarVariety").get<std::string>(),
        field + ".StellarVariety");
  check(actual.planet_bearing_systems ==
            expected.at("PlanetBearingSystems").get<std::string>(),
        field + ".PlanetBearingSystems");
  check(actual.habitable_worlds ==
            expected.at("HabitableWorlds").get<std::string>(),
        field + ".HabitableWorlds");
  check(actual.guaranteed_nearby_habitable_worlds ==
            expected.at("GuaranteedNearbyHabitableWorlds").get<int>(),
        field + ".GuaranteedNearbyHabitableWorlds");
  check(actual.other_civilizations ==
            expected.at("OtherCivilizations").get<int>(),
        field + ".OtherCivilizations");
  check(actual.ancient_civilizations ==
            expected.at("AncientCivilizations").get<std::string>(),
        field + ".AncientCivilizations");
  check(actual.space_hazards == expected.at("SpaceHazards").get<std::string>(),
        field + ".SpaceHazards");
  check(actual.starting_development ==
            expected.at("StartingDevelopment").get<std::string>(),
        field + ".StartingDevelopment");
  check(actual.difficulty == expected.at("Difficulty").get<std::string>(),
        field + ".Difficulty");
  check(actual.art_profile_version ==
            expected.at("ArtProfileVersion").get<std::string>(),
        field + ".ArtProfileVersion");
  check(actual.player_species_id.has_value() !=
            expected.at("PlayerSpeciesId").is_null(),
        field + ".PlayerSpeciesId presence");
  if (actual.player_species_id)
    check(*actual.player_species_id ==
              expected.at("PlayerSpeciesId").get<std::string>(),
          field + ".PlayerSpeciesId");
  check(actual.anomaly_frequency ==
            expected.at("AnomalyFrequency").get<std::string>(),
        field + ".AnomalyFrequency");
  check(actual.galactic_core.has_value() !=
            expected.at("GalacticCore").is_null(),
        field + ".GalacticCore presence");
  if (actual.galactic_core)
    equal_named_core(*actual.galactic_core, expected.at("GalacticCore"),
                     field + ".GalacticCore");
  auto lower = [](std::string value) {
    std::ranges::transform(value, value.begin(), [](unsigned char character) {
      return static_cast<char>(std::tolower(character));
    });
    return value;
  };
  const auto summary =
      std::to_string(actual.system_count) + " systems · " +
      lower(actual.stellar_variety) + " stellar variety · " +
      lower(actual.habitable_worlds) + " habitable worlds · " +
      std::to_string(actual.other_civilizations) +
      " other civilizations · " + lower(actual.ancient_civilizations) +
      " ancient powers";
  check(summary == expected.at("SpoilerFreeSummary").get<std::string>(),
        field + ".SpoilerFreeSummary");
}

PersistableFreshCampaignOptions decode_options(const Json &arguments) {
  check(arguments.at("SettingsProfile") ==
            "GalaxyGenerationMetadata.FullGalaxy500.ToSettings + civilization overrides",
        "Unsupported settings profile.");
  return {
      arguments.at("CreatedAtUtc").get<std::string>(),
      arguments.at("SystemCount").get<int>(),
      arguments.at("PreWarpCount").get<int>(),
      arguments.at("AncientCount").get<int>(),
      arguments.at("PlayerSpeciesId").get<std::string>(),
  };
}

ErrorInfo expected_error(const Json &error) {
  return {error.at("Type").get<std::string>(),
          error.at("Message").get<std::string>()};
}

bool equal_options(const PersistableFreshCampaignOptions &left,
                   const PersistableFreshCampaignOptions &right) {
  return left.created_at_utc == right.created_at_utc &&
         left.system_count == right.system_count &&
         left.pre_warp_civilization_count ==
             right.pre_warp_civilization_count &&
         left.ancient_civilization_count ==
             right.ancient_civilization_count &&
         left.player_species_id == right.player_species_id;
}

} // namespace

int main(int argc, char **argv) {
  const auto cwd = std::filesystem::current_path();
  const std::string root_argument = argc > 1 ? argv[1] : "<missing>";
  const std::string fixture_argument = argc > 2 ? argv[2] : "<missing>";
  const std::string catalog_argument = argc > 3 ? argv[3] : "<missing>";
  try {
    check(argc == 4,
          "Expected source root, retained fixture, and stellar catalog.");
    const auto root = std::filesystem::absolute(root_argument);
    const auto fixture_path = std::filesystem::absolute(fixture_argument);
    const auto fixture_bytes = bytes(fixture_path);
    check(sha256(fixture_bytes) == fixture_sha, "Fixture SHA-256 mismatch.");
    const auto fixture = Json::parse(fixture_bytes);
    check(fixture.at("Schema") ==
              "stellar.persistable-fresh-campaign.actual-source.v1",
          "Unsupported fixture schema.");
    check(fixture.at("RowCount").get<std::size_t>() ==
              fixture.at("Cases").size(),
          "Fixture row accounting mismatch.");
    check(fixture.at("SourceHashesBefore") == fixture.at("SourceHashesAfter"),
          "Fixture source hashes changed during capture.");
    for (const auto &[relative, hash] :
         fixture.at("SourceHashesBefore").items())
      check(sha256(bytes(root / relative)) == hash.get<std::string>(),
            "Source SHA-256 mismatch: " + relative);
    const auto catalog = load_nearby_catalog(catalog_argument);
    check(!catalog.empty(), "Stellar catalog must not be empty.");

    std::size_t passed{};
    std::size_t successful{};
    std::size_t failed{};
    for (const auto &test : fixture.at("Cases")) {
      const auto name = test.at("Name").get<std::string>();
      const auto &arguments = test.at("Arguments");
      const auto seed = arguments.at("Seed").get<std::int64_t>();
      auto options = decode_options(arguments);
      const auto original_options = options;
      check(test.at("SettingsBefore") == test.at("SettingsAfter"),
            name + ": source settings mutation");
      const auto &settings = test.at("SettingsBefore");
      check(settings.at("SystemCount") == options.system_count &&
                settings.at("GalaxyShape") == "FullGalaxy" &&
                settings.at("IncludeGalacticCore") == true &&
                settings.at("InitialPreWarpSensorRange") == 8 &&
                settings.at("InitialAncientSensorRange") == 25 &&
                settings.at("PreWarpCivilizationCount") ==
                    options.pre_warp_civilization_count &&
                settings.at("AncientCivilizationCount") ==
                    options.ancient_civilization_count &&
                settings.at("PlayerSpeciesId") == options.player_species_id,
            name + ": source settings boundary mismatch");
      equal_number(number(settings.at("HabitableChance")), .16,
                   name + ".Settings.HabitableChance");
      equal_number(number(settings.at("AnomalyChance")), .20,
                   name + ".Settings.AnomalyChance");
      if (options.system_count == 250 || options.system_count == 500 ||
          options.system_count == 1000 || options.system_count == 2500) {
        const auto profile_core = full_galaxy_core(options.system_count);
        equal_number(number(settings.at("Radius")),
                     profile_core.exclusion_radius / .14F,
                     name + ".Settings.Radius");
      }
      const auto has_expected_error = !test.at("Error").is_null();
      std::optional<ErrorInfo> wanted;
      if (has_expected_error)
        wanted = expected_error(test.at("Error"));

      std::optional<FreshCampaignState> campaign;
      std::optional<ErrorInfo> actual_error;
      try {
        campaign = seed_persistable_fresh_campaign(seed, catalog, options);
      } catch (const std::out_of_range &error) {
        actual_error = {"ArgumentOutOfRangeException", error.what()};
      } catch (const std::invalid_argument &error) {
        actual_error = {"ArgumentException", error.what()};
      } catch (const std::exception &error) {
        fail(name + ": unexpected native exception " + typeid(error).name() +
             ": " + error.what());
      }

      check(equal_options(options, original_options),
            name + ": native factory mutated options");

      check(actual_error.has_value() == has_expected_error,
            name + ": error presence mismatch");
      if (wanted) {
        check(actual_error->type == wanted->type,
              name + ": error type mismatch: " + actual_error->type);
        check(actual_error->message == wanted->message,
              name + ": error message mismatch: " + actual_error->message);
        check(test.at("Result").is_null(), name + ": failure returned a result");
        ++failed;
      } else {
        check(campaign.has_value(), name + ": missing campaign");
        const auto &expected = test.at("Result");
        equal_campaign(*campaign, expected, name);
        check(campaign->generation_metadata.has_value(),
              name + ": generation metadata missing");
        check(campaign->galactic_core.has_value(),
              name + ": named galactic core missing");
        equal_metadata(*campaign->generation_metadata,
                       expected.at("GenerationMetadata"),
                       name + ".GenerationMetadata");
        equal_named_core(*campaign->galactic_core, expected.at("Core"),
                         name + ".NamedCore");
        check(campaign->generation_metadata->galactic_core ==
                  campaign->galactic_core,
              name + ": metadata/state named cores disagree");
        check(campaign->core.has_value(), name + ": geometric core missing");
        equal_number(campaign->core->position.x, campaign->galactic_core->x,
                     name + ".CoreAgreement.X");
        equal_number(campaign->core->position.y, campaign->galactic_core->y,
                     name + ".CoreAgreement.Y");
        equal_number(campaign->core->exclusion_radius,
                     campaign->galactic_core->exclusion_radius,
                     name + ".CoreAgreement.ExclusionRadius");
        check(campaign->combat_intelligence.empty(),
              name + ": factory added combat intelligence");
        for (const auto &civilization : campaign->civilizations)
          check(campaign->knowledge.is_system_fully_surveyed(
                    civilization.id, civilization.home_system_id),
                name + ": civilization home is not fully surveyed");

        const auto retained_timestamp = options.created_at_utc;
        const auto retained_species = options.player_species_id;
        options.created_at_utc.assign("mutated timestamp");
        options.player_species_id.assign("mutated species");
        check(campaign->generation_metadata->created_at_utc == retained_timestamp,
              name + ": timestamp is not owned");
        check(campaign->generation_metadata->player_species_id == retained_species,
              name + ": species is not owned");

        auto repeated_options = decode_options(arguments);
        const auto repeated = seed_persistable_fresh_campaign(
            seed, catalog, repeated_options);
        equal_campaign(repeated, expected, name + ".Repeated");
        equal_metadata(*repeated.generation_metadata,
                       expected.at("GenerationMetadata"),
                       name + ".Repeated.GenerationMetadata");
        check(repeated.galactic_core == campaign->galactic_core,
              name + ": repeated named core mismatch");
        ++successful;
      }
      ++passed;
    }
    check(passed == 8 && successful == 5 && failed == 3,
          "Native row accounting mismatch.");
    std::cout << "persistable fresh campaign parity passed: " << passed
              << " rows (" << successful << " success, " << failed
              << " expected failure)\n";
    return 0;
  } catch (const std::exception &error) {
    std::cerr << "persistable fresh campaign parity failure: "
              << typeid(error).name() << ": " << error.what() << '\n'
              << "cwd: " << cwd.string() << '\n'
              << "source root: "
              << (root_argument == "<missing>"
                      ? root_argument
                      : std::filesystem::absolute(root_argument).string())
              << '\n'
              << "fixture: "
              << (fixture_argument == "<missing>"
                      ? fixture_argument
                      : std::filesystem::absolute(fixture_argument).string())
              << '\n'
              << "catalog: "
              << (catalog_argument == "<missing>"
                      ? catalog_argument
                      : std::filesystem::absolute(catalog_argument).string())
              << '\n';
    return 1;
  }
}
