#pragma once
#include <filesystem>
#include <memory>
#include <string>
#include <string_view>
namespace stellar::engine {
// Process-scoped local diagnostics. Mirrors existing console streams rather
// than replacing them, so automated launch tools retain their output contract.
class RuntimeDiagnostics final {
public:
  RuntimeDiagnostics(std::string_view game_version,std::string_view engine_version,
                     std::filesystem::path directory={}) noexcept;
  ~RuntimeDiagnostics();
  RuntimeDiagnostics(const RuntimeDiagnostics&)=delete;
  RuntimeDiagnostics& operator=(const RuntimeDiagnostics&)=delete;
  void fatal(std::string_view message) noexcept;
  static void context(std::string_view text) noexcept;
  // Called once per frame by the host's UI loop. The first beat arms the hang
  // watchdog (STELLAR_WATCHDOG_MS, default 30 s, 0 disables): a stalled loop
  // gets a one-shot HANG report + thread stack + minidump, then rearms.
  static void heartbeat() noexcept;
  // Best-effort "module.exe+0xOFF function+0xD file(line)" description of a
  // code address: dbghelp symbols when PDBs sit beside the binary, degrading
  // to module+offset otherwise (and a raw pointer off-module). The fault
  // reporter emits this next to the raw exception address; safe to probe.
  [[nodiscard]] std::string describe_address(const void* address) const noexcept;
  // Directory this session's log/report/dump trio lives in — empty when no
  // diagnostics session is active. Support-bundle exporters use it to attach
  // the newest crash report without hardcoding the log path.
  [[nodiscard]] static std::filesystem::path diagnostics_directory() noexcept;
  // The live session's own report path — non-empty and header-only from
  // construction until a fault writes it (removed on clean exit). Bundle
  // exporters must exclude it: it carries no crash evidence.
  [[nodiscard]] static std::filesystem::path active_report_path() noexcept;
  [[nodiscard]] std::filesystem::path log_path() const;
private:
  struct Impl;std::unique_ptr<Impl> impl_;
};
}
