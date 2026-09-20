#include <stellar/core/strategic_clock.hpp>
#include <stellar/core/detail/adaptive_research_sha256.hpp>

#include <nlohmann/json.hpp>

#include <cmath>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <span>
#include <stdexcept>
#include <string>
#include <vector>

namespace fs = std::filesystem;
using Json = nlohmann::json;
using namespace stellar::core;

namespace {
std::string read_bytes(const fs::path &path) {
  std::ifstream input(path, std::ios::binary);
  if (!input) throw std::runtime_error("Cannot read " + path.string());
  return {std::istreambuf_iterator<char>(input), {}};
}
std::string sha256(std::string_view text) {
  const auto bytes = std::span<const std::uint8_t>(
      reinterpret_cast<const std::uint8_t *>(text.data()), text.size());
  const auto digest = detail::adaptive_research_sha256(bytes);
  constexpr char hex[] = "0123456789ABCDEF"; std::string result; result.reserve(64);
  for (auto value : digest) { result += hex[value >> 4]; result += hex[value & 15]; }
  return result;
}
Json scalar(double value) {
  if (std::isnan(value)) return "NaN";
  if (std::isinf(value)) return value < 0. ? "-Infinity" : "Infinity";
  if (value == 0. && std::signbit(value)) return "-0";
  return value;
}
Json clock_state(const StrategicClock &value) {
  return {{"Speed", static_cast<int>(value.speed())}, {"ResumeSpeed", static_cast<int>(value.resume_speed())},
          {"SimulationDays", scalar(value.simulation_days())}, {"EffectiveMultiplier", scalar(value.effective_multiplier())},
          {"RequestedMultiplier", scalar(value.requested_multiplier())}, {"BacklogDays", scalar(value.backlog_days())}};
}
Json autosave_state(const CampaignAutosaveScheduler &value) { return {{"NextDueDay", scalar(value.next_due_day())}}; }
void equal(const Json &actual, const Json &expected, const std::string &label) {
  if (actual.is_number() && expected.is_number()) {
    const auto a = actual.get<double>(), b = expected.get<double>();
    if (std::abs(a - b) <= 1e-10 * std::max({1., std::abs(a), std::abs(b)})) return;
  } else if (actual.type() == expected.type() && actual.is_object() && actual.size() == expected.size()) {
    for (const auto &[key, value] : actual.items()) {
      if (!expected.contains(key)) throw std::runtime_error(label + " missing key " + key);
      equal(value, expected.at(key), label + "/" + key);
    }
    return;
  } else if (actual == expected) return;
  throw std::runtime_error(label + " differs: actual=" + actual.dump() + " expected=" + expected.dump());
}
std::string error_category(const std::exception &value) {
  if (dynamic_cast<const std::invalid_argument *>(&value)) return "ArgumentOutOfRangeException";
  if (dynamic_cast<const std::out_of_range *>(&value)) return "IndexOutOfRangeException";
  if (dynamic_cast<const std::overflow_error *>(&value)) return "OverflowException";
  return typeid(value).name();
}
void apply_clock(StrategicClock &clock, const Json &row, Json &result) {
  const auto &input = row.at("Input"); const auto operation = row.at("Operation").get<std::string>();
  if (operation == "Restore") clock.restore(input.at("Days"));
  else if (operation == "RestoreNaN") clock.restore(std::numeric_limits<double>::quiet_NaN());
  else if (operation == "SetSpeed") clock.set_speed(static_cast<StrategicSpeed>(input.at("Speed").get<int>()));
  else if (operation == "Resume") clock.resume();
  else if (operation == "SelectResumeSpeed") clock.select_resume_speed(static_cast<StrategicSpeed>(input.at("Speed").get<int>()));
  else if (operation == "Advance") result = scalar(clock.advance(input.at("Seconds"), input.at("MaximumStepDays")));
  else if (operation == "AdvanceNaN") result = scalar(clock.advance(std::numeric_limits<double>::quiet_NaN(), input.at("MaximumStepDays")));
  else if (operation == "AdvanceNegativeZeroBudget") result = scalar(clock.advance(input.at("Seconds"), -0.));
  else if (operation == "AdvanceNaNBudget") result = scalar(clock.advance(input.at("Seconds"), std::numeric_limits<double>::quiet_NaN()));
  else if (operation == "AdvanceDeveloperFrame") result = advance_developer_frame(clock, input.at("Seconds"));
  else if (operation == "AdvanceBoundedFrame" || operation == "AdvanceBoundedFrameInvalid") result = clock.advance_bounded_frame(input.at("Seconds"), input.at("MaximumDays"), input.at("MaximumBacklogDays"));
  else throw std::runtime_error("Unknown clock operation");
}
void apply_autosave(CampaignAutosaveScheduler &autosave, const Json &row, Json &result) {
  const auto &input = row.at("Input"); const auto operation = row.at("Operation").get<std::string>();
  const auto days = input.at("Days").get<double>();
  if (operation == "Reset" || operation == "ResetInvalid" || operation == "DeveloperReset") autosave.reset(days);
  else if (operation == "IsDue") result = autosave.is_due(days);
  else if (operation == "MarkSuccess") autosave.mark_success(days);
  else if (operation == "MarkFailure" || operation == "DeveloperMarkFailure") autosave.mark_failure(days);
  else throw std::runtime_error("Unknown autosave operation");
}
int run(const fs::path &fixture_path, const fs::path &source_root) {
  // Native play speeds cap at 4x; historical replay retains its 8x default.
  StrategicClock native_clock;
  native_clock.set_maximum_multiplier(4.);
  native_clock.set_speed(StrategicSpeed::Maximum);
  equal(native_clock.advance_bounded_frame(.125, 1., 2.), .5, "Native 4x elapsed days");
  native_clock.set_speed(StrategicSpeed::Paused);
  equal(native_clock.advance_bounded_frame(.125, 1., 2.), 0., "Native paused clock");
  native_clock.resume();
  equal(native_clock.requested_multiplier(), 4., "Native resumed 4x speed");
  bool invalid_multiplier_rejected=false;
  try { native_clock.set_maximum_multiplier(0.); }
  catch(const std::invalid_argument&) { invalid_multiplier_rejected=true; }
  if(!invalid_multiplier_rejected)throw std::runtime_error("Invalid native speed multiplier accepted.");
  const auto fixture_bytes = read_bytes(fixture_path); const auto fixture_hash = sha256(fixture_bytes);
  const auto fixture = Json::parse(fixture_bytes);
  if (fixture.at("SchemaVersion") != 1 || fixture.at("RowCount").get<int>() != static_cast<int>(fixture.at("Rows").size()))
    throw std::runtime_error("Fixture schema or row count is invalid.");
  std::vector<std::pair<fs::path, std::string>> sources;
  for (const auto &item : fixture.at("SourceFiles")) {
    const auto path = source_root / item.at("Path").get<std::string>(); const auto expected = item.at("Sha256").get<std::string>();
    if (sha256(read_bytes(path)) != expected) throw std::runtime_error("Source fingerprint mismatch: " + path.string());
    sources.emplace_back(path, expected);
  }
  StrategicClock clock; CampaignAutosaveScheduler autosave; CampaignAutosaveScheduler developer_autosave = CampaignAutosaveScheduler::developer_demo(); int passed = 0;
  for (const auto &row : fixture.at("Rows")) {
    const auto operation = row.at("Operation").get<std::string>(); const bool policy = operation == "Policy";
    const bool invalid_speed = operation == "InvalidSpeedLookup";
    const bool poisoned_developer_frame = operation == "AdvanceDeveloperFramePoisoned";
    const bool developer = operation == "DeveloperReset" || operation == "DeveloperMarkFailure";
    auto &scheduler = developer ? developer_autosave : autosave;
    StrategicClock poisoned; if (poisoned_developer_frame) { poisoned.set_speed(StrategicSpeed::Demo); poisoned.advance(0., std::numeric_limits<double>::quiet_NaN()); }
    const auto before = poisoned_developer_frame ? clock_state(poisoned) : invalid_speed ? Json{{"Speed", 99}, {"ResumeSpeed", 99}} : policy ? Json::object() : operation == "Reset" || operation == "ResetInvalid" || operation == "DeveloperReset" || operation == "IsDue" || operation == "MarkSuccess" || operation == "MarkFailure" || operation == "DeveloperMarkFailure" ? autosave_state(scheduler) : clock_state(clock);
    equal(before, row.at("Before"), row.at("Name").get<std::string>() + " before");
    Json result = nullptr; std::string category;
    try {
      if (poisoned_developer_frame) result = advance_developer_frame(poisoned, row.at("Input").at("Seconds"));
      else if (invalid_speed) { StrategicClock invalid; invalid.set_speed(static_cast<StrategicSpeed>(99)); static_cast<void>(invalid.requested_multiplier()); }
      else if (policy) { const auto &i = row.at("Input"); CampaignAutosaveScheduler candidate(CampaignAutosavePolicy(i.at("IntervalDays"), i.at("FailureRetryDays"))); }
      else if (operation == "Reset" || operation == "ResetInvalid" || operation == "DeveloperReset" || operation == "IsDue" || operation == "MarkSuccess" || operation == "MarkFailure" || operation == "DeveloperMarkFailure") apply_autosave(scheduler, row, result);
      else apply_clock(clock, row, result);
    } catch (const std::exception &error) { category = error_category(error); }
    const auto after = poisoned_developer_frame ? clock_state(poisoned) : invalid_speed ? Json{{"Speed", 99}, {"ResumeSpeed", 99}} : policy ? Json::object() : operation == "Reset" || operation == "ResetInvalid" || operation == "DeveloperReset" || operation == "IsDue" || operation == "MarkSuccess" || operation == "MarkFailure" || operation == "DeveloperMarkFailure" ? autosave_state(scheduler) : clock_state(clock);
    equal(after, row.at("After"), row.at("Name").get<std::string>() + " after");
    if (row.at("ErrorType").is_null()) {
      if (!category.empty()) throw std::runtime_error("Unexpected exception " + category + " in " + row.at("Name").get<std::string>());
      equal(result, row.at("Result"), row.at("Name").get<std::string>() + " result");
    }
    else if (category != row.at("ErrorType").get<std::string>()) throw std::runtime_error("Exception category mismatch: " + row.at("Name").get<std::string>());
    ++passed;
  }
  if (sha256(read_bytes(fixture_path)) != fixture_hash) throw std::runtime_error("Fixture changed during replay.");
  for (const auto &[path, expected] : sources) if (sha256(read_bytes(path)) != expected) throw std::runtime_error("Source changed during replay: " + path.string());
  std::cout << "Strategic clock parity: " << passed << "/" << fixture.at("RowCount") << " rows passed\n"; return 0;
}
}
int main(int argc, char **argv) {
  if (argc != 3) { std::cerr << "Usage: strategic_clock_tests <fixture.json> <source-root>\n"; return 1; }
  try { return run(fs::absolute(argv[1]), fs::absolute(argv[2])); }
  catch (const std::exception &error) { std::cerr << error.what() << "\n"; return 1; }
}
