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
  // Best-effort "module.exe+0xOFF function+0xD file(line)" description of a
  // code address: dbghelp symbols when PDBs sit beside the binary, degrading
  // to module+offset otherwise (and a raw pointer off-module). The fault
  // reporter emits this next to the raw exception address; safe to probe.
  [[nodiscard]] std::string describe_address(const void* address) const noexcept;
  [[nodiscard]] std::filesystem::path log_path() const;
private:
  struct Impl;std::unique_ptr<Impl> impl_;
};
}
