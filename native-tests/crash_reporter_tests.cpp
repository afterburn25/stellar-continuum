#include <stellar/engine/crash_reporter.hpp>

#include <filesystem>
#include <fstream>
#include <iostream>
#include <sstream>
#include <string>

using namespace stellar::engine;

namespace {
int failures = 0;
void check(bool condition, const char *message) {
  if (!condition) {
    std::cerr << "FAIL: " << message << '\n';
    ++failures;
  }
}
std::string read_all(const std::filesystem::path &path) {
  std::ifstream in(path, std::ios::binary);
  std::ostringstream contents;
  contents << in.rdbuf();
  return contents.str();
}
} // namespace

int main() {
  const auto root = std::filesystem::temp_directory_path() /
                    "stellar_crash_reporter_tests";
  std::filesystem::remove_all(root);

  CrashReporter::Options options;
  options.crash_directory = root.string();
  options.game_version = "0.1.0-test";
  options.engine_version = "foundation-expansion";
  options.build_id = "test-build";
  options.recent_event_limit = 3;
  CrashReporter reporter(options);

  reporter.set_context("os", "windows");
  reporter.set_context("gpu", "test-adapter");
  reporter.set_context("os", "windows-11"); // overwrite
  reporter.set_simulation_seed(20260908);
  for (int i = 0; i < 5; ++i)
    reporter.log_event("event_" + std::to_string(i));

  const auto bundle = reporter.write_diagnostic_bundle("unit test");
  check(bundle.has_value(), "bundle written");
  const auto dir = std::filesystem::path(*bundle);
  check(std::filesystem::exists(dir / "manifest.txt"), "manifest exists");
  check(std::filesystem::exists(dir / "context.json"), "context exists");
  check(std::filesystem::exists(dir / "recent_events.txt"),
        "event log exists");

  const auto manifest = read_all(dir / "manifest.txt");
  check(manifest.find("game_version: 0.1.0-test") != std::string::npos,
        "manifest carries version");
  check(manifest.find("simulation_seed: 20260908") != std::string::npos,
        "manifest carries seed");

  const auto context = read_all(dir / "context.json");
  check(context.find("\"os\":\"windows-11\"") != std::string::npos,
        "context overwrite kept latest");
  check(context.find("\"gpu\":\"test-adapter\"") != std::string::npos,
        "context carries gpu");

  const auto events = read_all(dir / "recent_events.txt");
  check(events.find("event_0") == std::string::npos,
        "event ring evicts oldest");
  check(events.find("event_4") != std::string::npos,
        "event ring keeps newest");

  check(reporter.install(), "install registers filter");

  std::filesystem::remove_all(root);
  if (failures == 0)
    std::cout << "CrashReporter tests passed\n";
  return failures == 0 ? 0 : 1;
}
