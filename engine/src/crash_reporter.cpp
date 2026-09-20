#include <stellar/engine/crash_reporter.hpp>

#include <chrono>
#include <filesystem>
#include <fstream>
#include <sstream>

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
// DbgHelp for MiniDumpWriteDump; linked via target_link_libraries where the
// engine target adds Dbghelp.
#include <dbghelp.h>
#endif

namespace stellar::engine {

CrashReporter *CrashReporter::active_ = nullptr;

namespace {

std::string timestamp_id() {
  const auto now = std::chrono::system_clock::now();
  const auto time = std::chrono::system_clock::to_time_t(now);
  std::tm tm{};
  localtime_s(&tm, &time);
  char buffer[32];
  std::snprintf(buffer, sizeof(buffer), "%04d%02d%02d_%02d%02d%02d",
                tm.tm_year + 1900, tm.tm_mon + 1, tm.tm_mday, tm.tm_hour,
                tm.tm_min, tm.tm_sec);
  return buffer;
}

std::string json_escape(std::string_view text) {
  std::string out;
  for (const char c : text) {
    switch (c) {
    case '"': out += "\\\""; break;
    case '\\': out += "\\\\"; break;
    case '\n': out += "\\n"; break;
    default: out += c;
    }
  }
  return out;
}

} // namespace

CrashReporter::CrashReporter(Options options) : options_(std::move(options)) {}
CrashReporter::~CrashReporter() {
  if (active_ == this)
    active_ = nullptr;
}

void CrashReporter::set_context(std::string key, std::string value) {
  std::lock_guard lock(mutex_);
  for (auto &[k, v] : context_)
    if (k == key) {
      v = std::move(value);
      return;
    }
  context_.emplace_back(std::move(key), std::move(value));
}

void CrashReporter::record_event(std::string_view text) {
  recent_events_.emplace_back(text);
  if (recent_events_.size() > options_.recent_event_limit)
    recent_events_.erase(recent_events_.begin());
}

void CrashReporter::log_event(std::string_view text) {
  std::lock_guard lock(mutex_);
  record_event(text);
}

void CrashReporter::set_simulation_seed(std::uint64_t seed) {
  std::lock_guard lock(mutex_);
  simulation_seed_ = seed;
}

std::optional<std::string>
CrashReporter::write_bundle_contents(const std::string &directory,
                                     const std::string &reason,
                                     const std::string &exception_detail)
    const {
  namespace fs = std::filesystem;
  std::error_code ec;
  fs::create_directories(directory, ec);
  if (ec)
    return std::nullopt;

  std::vector<std::pair<std::string, std::string>> context;
  std::vector<std::string> events;
  std::uint64_t seed{};
  {
    std::lock_guard lock(mutex_);
    context = context_;
    events = recent_events_;
    seed = simulation_seed_;
  }

  std::ostringstream manifest;
  manifest << "Stellar Continuum diagnostic bundle\n"
           << "reason: " << reason << '\n'
           << "exception: " << exception_detail << '\n'
           << "game_version: " << options_.game_version << '\n'
           << "engine_version: " << options_.engine_version << '\n'
           << "build_id: " << options_.build_id << '\n'
           << "simulation_seed: " << seed << '\n';
  {
    std::ofstream out(fs::path(directory) / "manifest.txt",
                      std::ios::binary | std::ios::trunc);
    out << manifest.str();
  }
  {
    std::ofstream out(fs::path(directory) / "context.json",
                      std::ios::binary | std::ios::trunc);
    out << "{";
    bool first = true;
    for (const auto &[key, value] : context) {
      if (!first)
        out << ",";
      first = false;
      out << "\"" << json_escape(key) << "\":\"" << json_escape(value)
          << "\"";
    }
    out << "}";
  }
  {
    std::ofstream out(fs::path(directory) / "recent_events.txt",
                      std::ios::binary | std::ios::trunc);
    for (const auto &event : events)
      out << event << '\n';
  }
  return manifest.str();
}

std::optional<std::string>
CrashReporter::write_diagnostic_bundle(const std::string &reason) const {
  const auto directory =
      (std::filesystem::path(options_.crash_directory) /
       ("diagnostic_" + timestamp_id()))
          .string();
  if (!write_bundle_contents(directory, reason, "none"))
    return std::nullopt;
  return directory;
}

#ifdef _WIN32
namespace {
LONG WINAPI stellar_unhandled_filter(EXCEPTION_POINTERS *pointers) {
  auto *reporter = CrashReporter::active_reporter();
  if (reporter == nullptr)
    return EXCEPTION_CONTINUE_SEARCH;

  const auto base =
      std::filesystem::path(reporter->options().crash_directory) /
      ("crash_" + timestamp_id());
  std::error_code ec;
  std::filesystem::create_directories(base, ec);

  std::ostringstream detail;
  detail << "code=0x" << std::hex
         << pointers->ExceptionRecord->ExceptionCode;
  reporter->write_bundle_contents(base.string(), "unhandled exception",
                                  detail.str());

  HANDLE file = CreateFileW((base / "crash.dmp").wstring().c_str(),
                            GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS,
                            FILE_ATTRIBUTE_NORMAL, nullptr);
  if (file != INVALID_HANDLE_VALUE) {
    MINIDUMP_EXCEPTION_INFORMATION info{};
    info.ThreadId = GetCurrentThreadId();
    info.ExceptionPointers = pointers;
    info.ClientPointers = FALSE;
    MiniDumpWriteDump(GetCurrentProcess(), GetCurrentProcessId(), file,
                      MiniDumpWithIndirectlyReferencedMemory, &info, nullptr,
                      nullptr);
    CloseHandle(file);
  }
  return EXCEPTION_CONTINUE_SEARCH;
}
} // namespace

bool CrashReporter::install() {
  active_ = this;
  // The return value is the previous filter; nullptr simply means none was
  // installed, so installation always succeeds here.
  (void)SetUnhandledExceptionFilter(stellar_unhandled_filter);
  return true;
}
#else
bool CrashReporter::install() {
  active_ = this;
  return true;
}
#endif

} // namespace stellar::engine
