#pragma once

#include <cstdint>
#include <mutex>
#include <optional>
#include <string>
#include <vector>

namespace stellar::engine {

// Local crash/diagnostic capture. On Windows, installs an unhandled-exception
// filter that writes a minidump plus a diagnostic bundle under
// `<crash_dir>/<timestamped-id>/`. Nothing is uploaded; bundles stay local
// for the user to share. Also usable without a crash via
// write_diagnostic_bundle() (support-bundle captures from Developer Mode).
//
// Population API is intentionally small: the app feeds a rolling event log
// and context key/values which are snapshotted into the bundle.
class CrashReporter {
public:
  struct Options {
    std::string crash_directory;   // e.g. "<save_root>/crash_dumps"
    std::string game_version;
    std::string engine_version;
    std::string build_id;
    std::size_t recent_event_limit{64};
  };

  explicit CrashReporter(Options options);
  ~CrashReporter();

  CrashReporter(const CrashReporter &) = delete;
  CrashReporter &operator=(const CrashReporter &) = delete;

  // Installs the OS-level unhandled-exception filter (Windows). Safe to call
  // once at startup; a second call replaces the previous filter chain.
  bool install();

  // Context gathered into every bundle. Call from anywhere before a crash;
  // data is copied under a lock and read again inside the handler.
  void set_context(std::string key, std::string value);
  void log_event(std::string_view text);
  void set_simulation_seed(std::uint64_t seed);

  // Writes a diagnostic bundle (manifest.txt, context.json, recent_events.txt)
  // without a crash. Returns the bundle directory on success.
  std::optional<std::string>
  write_diagnostic_bundle(const std::string &reason) const;

  const Options &options() const noexcept { return options_; }

  // Test hook: renders the bundle contents to `directory` and returns the
  // manifest text. Used by tests and write_diagnostic_bundle.
  std::optional<std::string>
  write_bundle_contents(const std::string &directory,
                        const std::string &reason,
                        const std::string &exception_detail) const;

  // The reporter currently wired to the OS-level filter, if any.
  static CrashReporter *active_reporter() noexcept { return active_; }

private:
  Options options_;
  std::vector<std::pair<std::string, std::string>> context_;
  std::vector<std::string> recent_events_;
  std::uint64_t simulation_seed_{};
  mutable std::mutex mutex_;

  static CrashReporter *active_;
  void record_event(std::string_view text);
};

} // namespace stellar::engine
