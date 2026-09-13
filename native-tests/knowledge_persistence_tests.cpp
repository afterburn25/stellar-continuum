#include <array>
#include <cmath>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <limits>
#include <nlohmann/json.hpp>
#include <set>
#include <sstream>
#include <stellar/core/detail/adaptive_research_sha256.hpp>
#include <stellar/core/knowledge_persistence.hpp>
#include <typeinfo>

using Json = nlohmann::json;
using namespace stellar::core;
namespace fs = std::filesystem;
namespace {

double number(const Json &v) {
  if (v.is_number())
    return v.get<double>();
  const auto s = v.get<std::string>();
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
SystemSurveyPersistenceDto survey(const Json &v) {
  return {v.at("SystemId"),
          static_cast<SystemSurveyLevel>(v.at("Level").get<int>()),
          number(v.at("Progress"))};
}
Json survey_json(const SystemSurveyPersistenceDto &v) {
  return {{"SystemId", v.system_id},
          {"Level", static_cast<int>(v.level)},
          {"Progress", floating(v.progress)}};
}
CivilizationKnowledgePersistenceDto entry(const Json &v) {
  CivilizationKnowledgePersistenceDto r;
  r.civilization_id = v.at("CivilizationId");
  r.galactic_core_access_unlocked = v.at("GalacticCoreAccessUnlocked");
  r.galactic_core_explored = v.at("GalacticCoreExplored");
  if (!v.at("KnownSystemIds").is_null())
    r.known_system_ids = v.at("KnownSystemIds").get<std::vector<int>>();
  if (!v.at("KnownCivilizationIds").is_null())
    r.known_civilization_ids =
        v.at("KnownCivilizationIds").get<std::vector<int>>();
  if (!v.at("SystemSurveys").is_null()) {
    r.system_surveys.emplace();
    for (const auto &x : v.at("SystemSurveys"))
      r.system_surveys->push_back(x.is_null() ? std::nullopt
                                              : std::optional{survey(x)});
  }
  return r;
}
Json entry_json(const CivilizationKnowledgePersistenceDto &v) {
  Json surveys = v.system_surveys ? Json::array() : Json(nullptr);
  if (v.system_surveys)
    for (const auto &x : *v.system_surveys)
      surveys.push_back(x ? survey_json(*x) : Json(nullptr));
  return {{"CivilizationId", v.civilization_id},
          {"GalacticCoreAccessUnlocked", v.galactic_core_access_unlocked},
          {"GalacticCoreExplored", v.galactic_core_explored},
          {"KnownSystemIds",
           v.known_system_ids ? Json(*v.known_system_ids) : Json(nullptr)},
          {"KnownCivilizationIds", v.known_civilization_ids
                                       ? Json(*v.known_civilization_ids)
                                       : Json(nullptr)},
          {"SystemSurveys", surveys}};
}
Json input_json(const CivilizationKnowledgePersistenceInput &v) {
  Json entries = v.entries_present ? Json::array() : Json(nullptr);
  if (v.entries_present)
    for (const auto &x : v.entries)
      entries.push_back(x ? entry_json(*x) : Json(nullptr));
  return {{"EntriesPresent", v.entries_present}, {"Entries", entries}};
}
Json state_json(const CivilizationKnowledgeState &s) {
  auto snap = s.snapshot();
  std::set<int> ids;
  for (auto &x : snap.systems)
    ids.insert(x.observer_id);
  for (auto &x : snap.civilizations)
    ids.insert(x.observer_id);
  for (int x : s.galactic_core_observers())
    ids.insert(x);
  Json out = Json::array();
  for (int id : ids) {
    Json surveys = Json::array();
    for (auto &x : s.system_survey_knowledge(id))
      surveys.push_back({{"SystemId", x.system_id},
                         {"Level", static_cast<int>(x.level)},
                         {"Progress", floating(x.progress)}});
    out.push_back({{"Id", id},
                   {"CoreAccess", s.has_galactic_core_access(id)},
                   {"CoreExplored", s.is_galactic_core_discovered(id)},
                   {"KnownSystems", s.known_systems(id)},
                   {"KnownCivilizations", s.known_civilizations(id)},
                   {"Surveys", surveys}});
  }
  return out;
}
CivilizationKnowledgeState state(const Json &v) {
  CivilizationKnowledgeState s;
  for (const auto &o : v) {
    int id = o.at("Id");
    if (o.at("CoreAccess"))
      s.unlock_galactic_core_access(id);
    if (o.at("CoreExplored"))
      s.record_galactic_core_exploration(id);
    for (int x : o.at("KnownSystems").get<std::vector<int>>())
      s.reveal_system(id, x);
    for (const auto &x : o.at("Surveys")) {
      auto q = survey(x);
      switch (q.level) {
      case SystemSurveyLevel::unknown:
        break;
      case SystemSurveyLevel::detected:
        s.reveal_system(id, q.system_id);
        break;
      case SystemSurveyLevel::partially_surveyed:
        s.advance_system_survey(id, q.system_id, q.progress);
        break;
      case SystemSurveyLevel::fully_surveyed:
        s.mark_system_fully_surveyed(id, q.system_id);
        break;
      }
    }
    for (int x : o.at("KnownCivilizations").get<std::vector<int>>())
      s.reveal_civilization(id, x);
  }
  return s;
}
bool eq(const Json &a, const Json &b) {
  if (a.type() == b.type()) {
    if (a.is_number_float()) {
      double x = a, y = b;
      return (std::isnan(x) && std::isnan(y)) || std::abs(x - y) <= 1e-10;
    }
    if (a.is_array()) {
      if (a.size() != b.size())
        return false;
      for (size_t i = 0; i < a.size(); ++i)
        if (!eq(a[i], b[i]))
          return false;
      return true;
    }
    if (a.is_object()) {
      if (a.size() != b.size())
        return false;
      for (auto i = a.begin(); i != a.end(); ++i) {
        auto j = b.find(i.key());
        if (j == b.end() || !eq(i.value(), *j))
          return false;
      }
      return true;
    }
    return a == b;
  }
  bool ai = a.is_number_integer() || a.is_number_unsigned(),
       bi = b.is_number_integer() || b.is_number_unsigned();
  if (ai && bi) {
    if (a.is_number_unsigned() && b.is_number_unsigned())
      return a.get<uint64_t>() == b.get<uint64_t>();
    if (a.is_number_integer() && b.is_number_integer())
      return a.get<int64_t>() == b.get<int64_t>();
    if (a.is_number_unsigned()) {
      auto x = b.get<int64_t>();
      return x >= 0 && a.get<uint64_t>() == uint64_t(x);
    }
    auto x = a.get<int64_t>();
    return x >= 0 && b.get<uint64_t>() == uint64_t(x);
  }
  if (a.is_number() && b.is_number()) {
    double x = a, y = b;
    return (std::isnan(x) && std::isnan(y)) || std::abs(x - y) <= 1e-10;
  }
  return false;
}
void require(const Json &a, const Json &b, const std::string &n) {
  if (!eq(a, b))
    throw std::runtime_error(n + "\nactual " + a.dump() + "\nexpected " +
                             b.dump());
}
Json error(std::exception_ptr p) {
  if (!p)
    return nullptr;
  try {
    std::rethrow_exception(p);
  } catch (const KnowledgePersistenceDataError &e) {
    return {{"Type", "InvalidDataException"}, {"Message", e.what()}};
  } catch (const KnowledgePersistenceNullReferenceError &e) {
    return {{"Type", "NullReferenceException"}, {"Message", e.what()}};
  } catch (const KnowledgePersistenceRangeError &e) {
    return {{"Type", "ArgumentOutOfRangeException"}, {"Message", e.what()}};
  } catch (const std::exception &e) {
    return {{"Type", "UnexpectedNativeException"}, {"Message", e.what()}};
  }
}
Json expected_error(const Json &r) {
  return r.at("ErrorType").is_null() ? Json(nullptr)
                                     : Json{{"Type", r.at("ErrorType")},
                                            {"Message", r.at("ErrorMessage")}};
}
std::string bytes(const fs::path &p) {
  std::ifstream f(p, std::ios::binary);
  if (!f)
    throw std::runtime_error("Cannot open: " + p.string());
  return {std::istreambuf_iterator<char>(f), {}};
}
std::string sha(std::string_view s) {
  auto d = detail::adaptive_research_sha256(
      {reinterpret_cast<const uint8_t *>(s.data()), s.size()});
  std::ostringstream o;
  o << std::uppercase << std::hex << std::setfill('0');
  for (auto x : d)
    o << std::setw(2) << unsigned(x);
  return o.str();
}
void replay(const Json &r) {
  auto name = r.at("Name").get<std::string>(),
       op = r.at("Operation").get<std::string>();
  if (op != "Restore" && op != "Capture")
    throw std::runtime_error("unknown op");
  std::exception_ptr fail;
  Json result, after;
  if (op == "Restore") {
    CivilizationKnowledgePersistenceInput in;
    in.entries_present = r.at("Input").at("EntriesPresent");
    if (in.entries_present)
      for (const auto &x : r.at("Input").at("Entries"))
        in.entries.push_back(x.is_null() ? std::nullopt
                                         : std::optional{entry(x)});
    require(input_json(in), r.at("Before"), name + " before");
    std::optional<CivilizationKnowledgeState> out;
    try {
      out = restore_civilization_knowledge(in);
    } catch (...) {
      fail = std::current_exception();
    }
    after = input_json(in);
    if (out)
      result = state_json(*out);
    if (out && !r.at("BeforeNext").is_null()) {
      require(state_json(*out), r.at("BeforeNext"), name + " before next");
      bool changed = out->advance_system_survey(7, 4, .375);
      require(state_json(*out), r.at("AfterNext"), name + " after next");
      require(Json(changed), r.at("NextResult"), name + " next result");
    }
  } else {
    auto in = state(r.at("Input"));
    require(state_json(in), r.at("Before"), name + " before");
    CivilizationKnowledgePersistenceInput out;
    try {
      out = capture_civilization_knowledge(in);
    } catch (...) {
      fail = std::current_exception();
    }
    after = state_json(in);
    if (!fail) {
      if (!out.entries_present)
        throw std::runtime_error("capture absent");
      result = Json::array();
      for (auto &x : out.entries)
        result.push_back(x ? entry_json(*x) : Json(nullptr));
    }
  }
  require(after, r.at("After"), name + " after");
  require(error(fail), expected_error(r), name + " error");
  if (!fail)
    require(result, r.at("Result"), name + " result");
}

void ownership_probes() {
  CivilizationKnowledgePersistenceDto value;
  value.civilization_id = 3;
  value.known_system_ids = std::vector<int>{7};
  value.known_civilization_ids = std::vector<int>{};
  value.system_surveys =
      std::vector<std::optional<SystemSurveyPersistenceDto>>{};
  CivilizationKnowledgePersistenceInput input{true, {value}};
  auto restored = restore_civilization_knowledge(input);
  input.entries.front()->known_system_ids->front() = 9;
  if (!restored.is_system_fully_surveyed(3, 7) ||
      restored.is_system_known(3, 9))
    throw std::runtime_error("restored state aliases input DTO");

  auto captured = capture_civilization_knowledge(restored);
  restored.reveal_system(3, 11);
  if (!captured.entries.front() ||
      captured.entries.front()->known_system_ids !=
          std::optional<std::vector<int>>{{7}})
    throw std::runtime_error("captured DTO aliases source state");
}

int run(const fs::path &fixture, const fs::path &root) {
  std::string raw = bytes(fixture);
  constexpr auto hash =
      "780E6A03D3CE0F8AEF21EFBB05690AC231338570630900B4821A3A51869F87C9";
  if (sha(raw) != hash)
    throw std::runtime_error("fixture hash");
  auto d = Json::parse(raw);
  if (d.at("RowCount") != 24 || d.at("Rows").size() != 24 ||
      d.at("SourceFiles").size() != 2)
    throw std::runtime_error("metadata");
  constexpr std::array sources = {
      "Persistence/CampaignSaveService.cs",
      "Simulation/Knowledge/CivilizationKnowledgeState.cs"};
  std::vector<std::pair<fs::path, std::string>> fingerprints;
  for (size_t i = 0; i < 2; ++i) {
    const auto path = root / sources[i];
    const auto expected =
        d.at("SourceFiles")[i].at("Sha256").get<std::string>();
    if (d.at("SourceFiles")[i].at("Path") != sources[i] ||
        sha(bytes(path)) != expected)
      throw std::runtime_error("source hash");
    fingerprints.emplace_back(path, expected);
  }
  for (auto &r : d.at("Rows"))
    replay(r);
  ownership_probes();
  if (eq(Json(9007199254740993ULL), Json(9007199254740992ULL)))
    throw std::runtime_error("integer precision");
  for (const auto &[path, expected] : fingerprints)
    if (sha(bytes(path)) != expected)
      throw std::runtime_error("source changed during replay");
  if (sha(bytes(fixture)) != hash)
    throw std::runtime_error("fixture changed during replay");
  std::cout << "Campaign knowledge native replay: 24/24 rows and ownership "
               "probes passed.\n";
  return 0;
}
} // namespace
int main(int argc, char **argv) {
  try {
    if (argc != 3)
      throw std::invalid_argument(
          "Usage: knowledge_persistence_tests <fixture> <source-root>");
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
