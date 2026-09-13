#include <algorithm>
#include <array>
#include <cctype>
#include <cmath>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <limits>
#include <nlohmann/json.hpp>
#include <optional>
#include <ranges>
#include <sstream>
#include <stdexcept>
#include <stellar/core/campaign_foundation_persistence.hpp>
#include <stellar/core/detail/adaptive_research_sha256.hpp>
#include <string>
#include <string_view>
#include <vector>
using Json = nlohmann::json;
using namespace stellar::core;
namespace fs = std::filesystem;
namespace {
double number(const Json &v) {
  if (v.is_number())
    return v.get<double>();
  auto s = v.get<std::string>();
  if (s == "NaN")
    return std::numeric_limits<double>::quiet_NaN();
  if (s == "Infinity")
    return std::numeric_limits<double>::infinity();
  if (s == "-Infinity")
    return -std::numeric_limits<double>::infinity();
  throw std::runtime_error("bad number");
}
Json floating(double v) {
  if (std::isnan(v))
    return "NaN";
  if (v == std::numeric_limits<double>::infinity())
    return "Infinity";
  if (v == -std::numeric_limits<double>::infinity())
    return "-Infinity";
  return v;
}
template <class T> std::optional<T> optional(const Json &v) {
  return v.is_null() ? std::nullopt : std::optional<T>{v.get<T>()};
}
Json opt_string(const std::optional<std::string> &v) {
  return v ? Json(*v) : Json(nullptr);
}
template <class E> Json opt_enum(const std::optional<E> &v) {
  return v ? Json(static_cast<int>(*v)) : Json(nullptr);
}
Json opt_double(const std::optional<double> &v) {
  return v ? floating(*v) : Json(nullptr);
}
StellarSystemPersistenceDto system_dto(const Json &v) {
  auto text = [&](std::string_view key) {
    auto i = v.find(std::string(key));
    return i == v.end() || i->is_null() ? std::optional<std::string>{}
                                        : std::optional{i->get<std::string>()};
  };
  auto cls = [&](std::string_view key) {
    auto i = v.find(std::string(key));
    return i == v.end() || i->is_null()
               ? std::optional<StellarClass>{}
               : std::optional{static_cast<StellarClass>(i->get<int>())};
  };
  auto depth = [&]() {
    auto i = v.find("GalacticDepthLightYears");
    return i == v.end() || i->is_null() ? std::optional<double>{}
                                        : std::optional{number(*i)};
  };
  return {v.at("Id"),
          v.at("Name"),
          v.at("X"),
          v.at("Y"),
          static_cast<StarArchetype>(v.at("Archetype").get<int>()),
          v.at("HasHabitableWorld"),
          v.at("HasAnomaly"),
          v.at("HasRareResource"),
          v.at("HasPreWarpCivilization"),
          text("CatalogPresetId"),
          cls("StellarClass"),
          cls("SecondaryStellarClass"),
          cls("TertiaryStellarClass"),
          depth(),
          text("StellarCatalogId")};
}
Json system_dto_json(const StellarSystemPersistenceDto &v) {
  Json r = {{"Id", v.id},
            {"Name", v.name},
            {"X", floating(v.x)},
            {"Y", floating(v.y)},
            {"Archetype", static_cast<int>(v.archetype)},
            {"HasHabitableWorld", v.has_habitable_world},
            {"HasAnomaly", v.has_anomaly},
            {"HasRareResource", v.has_rare_resource},
            {"HasPreWarpCivilization", v.has_pre_warp_civilization}};
  if (v.galactic_depth_light_years)
    r["GalacticDepthLightYears"] = floating(*v.galactic_depth_light_years);
  if (v.stellar_catalog_id)
    r["StellarCatalogId"] = *v.stellar_catalog_id;
  if (v.catalog_preset_id)
    r["CatalogPresetId"] = *v.catalog_preset_id;
  if (v.stellar_class)
    r["StellarClass"] = static_cast<int>(*v.stellar_class);
  if (v.secondary_stellar_class)
    r["SecondaryStellarClass"] = static_cast<int>(*v.secondary_stellar_class);
  if (v.tertiary_stellar_class)
    r["TertiaryStellarClass"] = static_cast<int>(*v.tertiary_stellar_class);
  return r;
}
Json system_json(const StellarSystem &v) {
  return {{"Id", v.id},
          {"Name", v.name},
          {"X", floating(v.position.x)},
          {"Y", floating(v.position.y)},
          {"Archetype", static_cast<int>(v.archetype)},
          {"HasHabitableWorld", v.has_habitable_world},
          {"HasAnomaly", v.has_anomaly},
          {"HasRareResource", v.has_rare_resource},
          {"HasPreWarpCivilization", v.has_pre_warp_civilization},
          {"CatalogPresetId", opt_string(v.catalog_preset_id)},
          {"StellarClass", opt_enum(v.primary)},
          {"SecondaryStellarClass", opt_enum(v.secondary)},
          {"TertiaryStellarClass", opt_enum(v.tertiary)},
          {"GalacticDepthLightYears", opt_double(v.position.depth_light_years)},
          {"StellarCatalogId", opt_string(v.stellar_catalog_id)}};
}
StellarSystem system_state(const Json &v) {
  return {v.at("Id"),
          v.at("Name"),
          {v.at("X").get<float>(), v.at("Y").get<float>(),
           v.at("GalacticDepthLightYears").is_null()
               ? std::nullopt
               : std::optional{number(v.at("GalacticDepthLightYears"))}},
          v.at("StellarClass").is_null()
              ? std::nullopt
              : std::optional{static_cast<StellarClass>(
                    v.at("StellarClass").get<int>())},
          v.at("SecondaryStellarClass").is_null()
              ? std::nullopt
              : std::optional{static_cast<StellarClass>(
                    v.at("SecondaryStellarClass").get<int>())},
          v.at("TertiaryStellarClass").is_null()
              ? std::nullopt
              : std::optional{static_cast<StellarClass>(
                    v.at("TertiaryStellarClass").get<int>())},
          optional<std::string>(v.at("CatalogPresetId")),
          optional<std::string>(v.at("StellarCatalogId")),
          static_cast<StarArchetype>(v.at("Archetype").get<int>()),
          v.at("HasHabitableWorld"),
          v.at("HasAnomaly"),
          v.at("HasRareResource"),
          v.at("HasPreWarpCivilization")};
}
CivilizationCharacter character(const Json &v) {
  return {v.at("Id"), v.at("DisplayName"),
          optional<std::string>(v.at("VoiceProfileId")),
          optional<std::string>(v.at("Portrait"))};
}
CivilizationPersistenceDto civilization_dto(const Json &v) {
  CivilizationPersistenceDto r;
  r.leadership_present = !v.at("Leadership").is_null();
  if (r.leadership_present)
    for (auto i = v.at("Leadership").begin(); i != v.at("Leadership").end();
         ++i)
      r.leadership.push_back(
          {i.key(), i.value().is_null() ? std::nullopt
                                        : std::optional{character(i.value())}});
  r.id = v.at("Id");
  r.name = v.at("Name");
  r.home_system_id = v.at("HomeSystemId");
  r.archetype =
      static_cast<CivilizationArchetype>(v.at("Archetype").get<int>());
  r.traits = {number(v.at("Aggression")),
              number(v.at("Territoriality")),
              number(v.at("Greed")),
              number(v.at("ScientificCuriosity")),
              number(v.at("RiskTolerance")),
              number(v.at("SurvivalPriority")),
              v.at("HonorBound")};
  r.is_player = v.at("IsPlayer");
  r.development_stage = static_cast<CivilizationDevelopmentStage>(
      v.at("DevelopmentStage").get<int>());
  r.is_seeded_ancient = v.at("IsSeededAncient");
  r.expansion_allowed = v.at("ExpansionAllowed");
  r.neutral_unless_provoked = v.at("NeutralUnlessProvoked");
  r.species_id = v.at("SpeciesId");
  return r;
}
Json character_json(const CivilizationCharacter &v) {
  return {{"Id", v.id},
          {"DisplayName", v.display_name},
          {"VoiceProfileId", opt_string(v.voice_profile_id)},
          {"Portrait", opt_string(v.portrait)}};
}
Json civilization_dto_json(const CivilizationPersistenceDto &v) {
  Json leadership = v.leadership_present ? Json::object() : Json(nullptr);
  if (v.leadership_present)
    for (const auto &e : v.leadership)
      leadership[e.office] =
          e.character ? character_json(*e.character) : Json(nullptr);
  return {{"Leadership", leadership},
          {"Id", v.id},
          {"Name", v.name},
          {"HomeSystemId", v.home_system_id},
          {"Archetype", static_cast<int>(v.archetype)},
          {"Aggression", floating(v.traits.aggression)},
          {"Territoriality", floating(v.traits.territoriality)},
          {"Greed", floating(v.traits.greed)},
          {"ScientificCuriosity", floating(v.traits.scientific_curiosity)},
          {"RiskTolerance", floating(v.traits.risk_tolerance)},
          {"SurvivalPriority", floating(v.traits.survival_priority)},
          {"HonorBound", v.traits.honor_bound},
          {"IsPlayer", v.is_player},
          {"DevelopmentStage", static_cast<int>(v.development_stage)},
          {"IsSeededAncient", v.is_seeded_ancient},
          {"ExpansionAllowed", v.expansion_allowed},
          {"NeutralUnlessProvoked", v.neutral_unless_provoked},
          {"SpeciesId", v.species_id}};
}
Json civilization_json(const Civilization &v) {
  Json leadership = Json::array();
  for (const auto &e : v.leadership)
    leadership.push_back(
        {{"Office", e.office},
         {"Id", e.character.id},
         {"DisplayName", e.character.display_name},
         {"VoiceProfileId", opt_string(e.character.voice_profile_id)},
         {"Portrait", opt_string(e.character.portrait)}});
  return {{"Id", v.id},
          {"Name", v.name},
          {"HomeSystemId", v.home_system_id},
          {"Archetype", static_cast<int>(v.archetype)},
          {"Aggression", floating(v.traits.aggression)},
          {"Territoriality", floating(v.traits.territoriality)},
          {"Greed", floating(v.traits.greed)},
          {"ScientificCuriosity", floating(v.traits.scientific_curiosity)},
          {"RiskTolerance", floating(v.traits.risk_tolerance)},
          {"SurvivalPriority", floating(v.traits.survival_priority)},
          {"HonorBound", v.traits.honor_bound},
          {"IsPlayer", v.is_player},
          {"DevelopmentStage", static_cast<int>(v.development_stage)},
          {"IsSeededAncient", v.is_seeded_ancient},
          {"ExpansionAllowed", v.expansion_allowed},
          {"NeutralUnlessProvoked", v.neutral_unless_provoked},
          {"SpeciesId", v.species_id},
          {"Leadership", leadership}};
}
Civilization civilization_state(const Json &v) {
  Civilization r;
  r.id = v.at("Id");
  r.name = v.at("Name");
  r.home_system_id = v.at("HomeSystemId");
  r.archetype =
      static_cast<CivilizationArchetype>(v.at("Archetype").get<int>());
  r.traits = {number(v.at("Aggression")),
              number(v.at("Territoriality")),
              number(v.at("Greed")),
              number(v.at("ScientificCuriosity")),
              number(v.at("RiskTolerance")),
              number(v.at("SurvivalPriority")),
              v.at("HonorBound")};
  r.is_player = v.at("IsPlayer");
  r.development_stage = static_cast<CivilizationDevelopmentStage>(
      v.at("DevelopmentStage").get<int>());
  r.is_seeded_ancient = v.at("IsSeededAncient");
  r.expansion_allowed = v.at("ExpansionAllowed");
  r.neutral_unless_provoked = v.at("NeutralUnlessProvoked");
  r.species_id = v.at("SpeciesId");
  for (const auto &e : v.at("Leadership"))
    r.leadership.push_back({e.at("Office"), character(e)});
  return r;
}
template <class T, class F> Json array_json(const std::vector<T> &v, F f) {
  Json r = Json::array();
  for (const auto &i : v)
    r.push_back(f(i));
  return r;
}
bool int_equal(const Json &a, const Json &b) {
  if (a.is_number_unsigned()) {
    auto x = a.get<std::uint64_t>();
    if (b.is_number_unsigned())
      return x == b.get<std::uint64_t>();
    auto y = b.get<std::int64_t>();
    return y >= 0 && x == static_cast<std::uint64_t>(y);
  }
  auto x = a.get<std::int64_t>();
  if (!b.is_number_unsigned())
    return x == b.get<std::int64_t>();
  return x >= 0 && static_cast<std::uint64_t>(x) == b.get<std::uint64_t>();
}
bool equal(const Json &a, const Json &b, std::string &p) {
  if (a.type() != b.type()) {
    if (!a.is_number() || !b.is_number())
      return false;
    bool ai = a.is_number_integer() || a.is_number_unsigned(),
         bi = b.is_number_integer() || b.is_number_unsigned();
    if (ai && bi)
      return int_equal(a, b);
    return std::abs(a.get<double>() - b.get<double>()) <= 1e-10;
  }
  if (a.is_primitive()) {
    if (a.is_number_float())
      return a == b || std::abs(a.get<double>() - b.get<double>()) <= 1e-10;
    return a == b;
  }
  if (a.size() != b.size())
    return false;
  if (a.is_array()) {
    for (std::size_t i = 0; i < a.size(); ++i) {
      auto old = p;
      p += "/" + std::to_string(i);
      if (!equal(a[i], b[i], p))
        return false;
      p = old;
    }
    return true;
  }
  for (auto i = a.begin(); i != a.end(); ++i) {
    auto old = p;
    p += "/" + i.key();
    if (!b.contains(i.key()) || !equal(i.value(), b.at(i.key()), p))
      return false;
    p = old;
  }
  return true;
}
void require_equal(const Json &a, const Json &b, const std::string &name,
                   std::string_view field) {
  std::string p;
  if (!equal(a, b, p))
    throw std::runtime_error(name + " " + std::string(field) + " " + p);
}
Json error_json(std::exception_ptr e) {
  if (!e)
    return nullptr;
  try {
    std::rethrow_exception(e);
  } catch (const CampaignFoundationPersistenceDataError &x) {
    return {{"Type", "InvalidDataException"}, {"Message", x.what()}};
  } catch (const CampaignFoundationPersistenceNullArgumentError &x) {
    return {{"Type", "ArgumentNullException"}, {"Message", x.what()}};
  } catch (const CampaignFoundationPersistenceRangeError &x) {
    return {{"Type", "ArgumentOutOfRangeException"}, {"Message", x.what()}};
  } catch (const CampaignFoundationPersistenceArgumentError &x) {
    return {{"Type", "ArgumentException"}, {"Message", x.what()}};
  } catch (const CampaignFoundationPersistenceOperationError &x) {
    return {{"Type", "InvalidOperationException"}, {"Message", x.what()}};
  } catch (const std::exception &x) {
    return {{"Type", "UnexpectedNativeException"}, {"Message", x.what()}};
  }
}
std::string bytes(const fs::path &p) {
  std::ifstream s(p, std::ios::binary);
  if (!s)
    throw std::runtime_error("Cannot open: " + p.string());
  return {std::istreambuf_iterator<char>(s), {}};
}
std::string sha(std::string_view v) {
  auto d = detail::adaptive_research_sha256(
      {reinterpret_cast<const std::uint8_t *>(v.data()), v.size()});
  std::ostringstream o;
  o << std::hex << std::setfill('0');
  for (auto b : d)
    o << std::setw(2) << static_cast<unsigned>(b);
  auto s = o.str();
  std::ranges::transform(s, s.begin(), [](unsigned char c) {
    return static_cast<char>(std::toupper(c));
  });
  return s;
}
Json expected_error(const Json &r) {
  return r.at("ErrorType").is_null() ? Json(nullptr)
                                     : Json{{"Type", r.at("ErrorType")},
                                            {"Message", r.at("ErrorMessage")}};
}
void ownership_probes() {
  StellarSystemPersistenceDto system_dto_value;
  system_dto_value.id = 1;
  system_dto_value.name = "owned-system";
  auto systems = restore_stellar_systems(std::span{&system_dto_value, 1});
  system_dto_value.name = "mutated-dto";
  if (systems.front().name != "owned-system")
    throw std::runtime_error("system restore output aliases input");
  auto captured_systems = capture_stellar_systems(systems);
  systems.front().name = "mutated-state";
  if (captured_systems.front().name != "owned-system")
    throw std::runtime_error("system capture output aliases input");
  CivilizationPersistenceDto civilization_dto_value;
  civilization_dto_value.leadership_present = true;
  civilization_dto_value.leadership.push_back(
      {"Governor",
       CivilizationCharacter{"character", "Character", std::string{"voice"},
                             std::string{"portrait"}}});
  civilization_dto_value.id = 1;
  civilization_dto_value.name = "owned-civilization";
  civilization_dto_value.species_id = "terran_baseline";
  auto civilizations = restore_civilizations(
      std::span{&civilization_dto_value, 1}, false, 16, 1);
  civilization_dto_value.name = "mutated-dto";
  civilization_dto_value.leadership.front().character->display_name = "mutated";
  if (civilizations.front().name != "owned-civilization" ||
      civilizations.front().leadership.front().character.display_name !=
          "Character")
    throw std::runtime_error("civilization restore output aliases input");
  auto captured_civilizations = capture_civilizations(civilizations);
  civilizations.front().name = "mutated-state";
  civilizations.front().leadership.front().character.display_name =
      "mutated-state";
  if (captured_civilizations.front().name != "owned-civilization" ||
      captured_civilizations.front()
              .leadership.front()
              .character->display_name != "Character")
    throw std::runtime_error("civilization capture output aliases input");
}
void replay_row(const Json &r) {
  auto name = r.at("Name").get<std::string>(),
       op = r.at("Operation").get<std::string>();
  std::exception_ptr failure;
  Json before, after, result;
  if (op == "RestoreSystems") {
    std::vector<StellarSystemPersistenceDto> in;
    for (const auto &i : r.at("Input"))
      in.push_back(system_dto(i));
    before = array_json(in, system_dto_json);
    require_equal(before, r.at("Before"), name, "before");
    std::vector<StellarSystem> out;
    try {
      out = restore_stellar_systems(in);
    } catch (...) {
      failure = std::current_exception();
    }
    after = array_json(in, system_dto_json);
    if (!failure)
      result = array_json(out, system_json);
  } else if (op == "CaptureSystems") {
    std::vector<StellarSystem> in;
    for (const auto &i : r.at("Input"))
      in.push_back(system_state(i));
    before = array_json(in, system_json);
    require_equal(before, r.at("Before"), name, "before");
    std::vector<StellarSystemPersistenceDto> out;
    try {
      out = capture_stellar_systems(in);
    } catch (...) {
      failure = std::current_exception();
    }
    after = array_json(in, system_json);
    if (!failure)
      result = array_json(out, system_dto_json);
  } else if (op == "RestoreCivilizations") {
    std::vector<CivilizationPersistenceDto> in;
    for (const auto &i : r.at("Input").at("Dtos"))
      in.push_back(civilization_dto(i));
    const bool legacy = r.at("Input").at("LegacyAlreadyWarpCapable");
    const int version = r.at("Input").at("SaveFormatVersion");
    const auto seed = r.at("Input").at("CampaignSeed").get<std::int64_t>();
    before = {{"Dtos", array_json(in, civilization_dto_json)},
              {"LegacyAlreadyWarpCapable", legacy},
              {"SaveFormatVersion", version},
              {"CampaignSeed", seed}};
    require_equal(before, r.at("Before"), name, "before");
    std::vector<Civilization> out;
    try {
      out = restore_civilizations(in, legacy, version, seed);
    } catch (...) {
      failure = std::current_exception();
    }
    after = {{"Dtos", array_json(in, civilization_dto_json)},
             {"LegacyAlreadyWarpCapable", legacy},
             {"SaveFormatVersion", version},
             {"CampaignSeed", seed}};
    if (!failure)
      result = array_json(out, civilization_json);
  } else if (op == "CaptureCivilizations") {
    std::vector<Civilization> in;
    for (const auto &i : r.at("Input"))
      in.push_back(civilization_state(i));
    before = array_json(in, civilization_json);
    require_equal(before, r.at("Before"), name, "before");
    std::vector<CivilizationPersistenceDto> out;
    try {
      out = capture_civilizations(in);
    } catch (...) {
      failure = std::current_exception();
    }
    after = array_json(in, civilization_json);
    if (!failure)
      result = array_json(out, civilization_dto_json);
  } else
    throw std::runtime_error("unknown operation");
  require_equal(after, r.at("After"), name, "after");
  require_equal(error_json(failure), expected_error(r), name, "error");
  if (!failure)
    require_equal(result, r.at("Result"), name, "result");
}
int run(const fs::path &fixture, const fs::path &root) {
  std::string integer_path;
  if (equal(Json(9007199254740993ULL), Json(9007199254740992ULL), integer_path))
    throw std::runtime_error("integer comparator lost precision");
  auto raw = bytes(fixture);
  if (sha(raw) !=
      "7ED6F4E7318A8E66EE72008CFF40AADD3AA86B06C222803AA8E99F1092A039EA")
    throw std::runtime_error("fixture hash");
  auto f = Json::parse(raw);
  constexpr std::array expected_sources = {
      "Persistence/CampaignSaveService.cs",
      "Simulation/Models/StarSystemState.cs",
      "Simulation/Models/CivilizationState.cs",
      "Simulation/Models/CivilizationLeadershipState.cs",
      "Simulation/Species/SpeciesAssignmentPolicy.cs",
      "Simulation/Species/SpeciesCatalog.cs",
      "Simulation/Generation/SolCatalogPreset.cs"};
  if (f.at("SchemaVersion") != 1 || f.at("RowCount") != 35 ||
      f.at("Rows").size() != 35 ||
      f.at("SourceFiles").size() != expected_sources.size())
    throw std::runtime_error("metadata");
  std::vector<std::pair<fs::path, std::string>> fingerprints;
  for (std::size_t index = 0; index < expected_sources.size(); ++index) {
    const auto &s = f.at("SourceFiles")[index];
    if (s.at("Path") != expected_sources[index])
      throw std::runtime_error("source inventory");
    auto p = root / s.at("Path").get<std::string>();
    auto h = s.at("Sha256").get<std::string>();
    if (sha(bytes(p)) != h)
      throw std::runtime_error("source hash " + p.string());
    fingerprints.emplace_back(p, h);
  }
  for (const auto &r : f.at("Rows"))
    replay_row(r);
  ownership_probes();
  for (const auto &[p, h] : fingerprints)
    if (sha(bytes(p)) != h)
      throw std::runtime_error("source changed");
  std::cout << "Campaign foundation native replay: 35/35 rows and ownership "
               "probes passed.\n";
  return 0;
}
} // namespace
int main(int argc, char **argv) {
  try {
    if (argc != 3)
      throw std::invalid_argument(
          "Usage: campaign_foundation_persistence_tests <fixture> "
          "<source-root>");
    return run(fs::absolute(argv[1]), fs::absolute(argv[2]));
  } catch (const std::exception &e) {
    std::cerr << typeid(e).name() << ": " << e.what()
              << "\nWorking directory: " << fs::current_path()
              << "\nFixture path: "
              << (argc > 1 ? fs::absolute(argv[1]).string() : "<missing>")
              << "\nSource root: "
              << (argc > 2 ? fs::absolute(argv[2]).string() : "<missing>")
              << '\n';
    return 1;
  }
}
