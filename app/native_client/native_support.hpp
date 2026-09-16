#pragma once

#include <filesystem>
#include <string>
#include <string_view>

namespace stellar::native_support {

// Ports the reference SupportLogger (src/Game/Diagnostics/SupportLogger.cs):
// a per-session log + system-info file under the user data root and an
// on-demand support bundle ZIP containing both plus the campaign save.
class NativeSupportLog {
 public:
  struct SystemInfo {
    std::string game_version, platform, gpu_driver;
    int cpu_cores{}, system_ram_mb{}, display_count{};
  };

  NativeSupportLog(std::filesystem::path user_root, SystemInfo info);

  [[nodiscard]] const std::string &session_id() const noexcept {
    return session_id_;
  }
  [[nodiscard]] const std::filesystem::path &log_directory() const noexcept {
    return log_directory_;
  }
  [[nodiscard]] const std::filesystem::path &log_path() const noexcept {
    return log_path_;
  }
  [[nodiscard]] const std::filesystem::path &system_info_path() const noexcept {
    return system_info_path_;
  }
  [[nodiscard]] const std::filesystem::path &support_directory()
      const noexcept {
    return support_directory_;
  }

  void log(std::string_view category, std::string_view message);

  // Writes support-<session>-<yyyymmdd-hhmmss>.zip under the support
  // directory and returns its path. Entries mirror the reference: the
  // session log, the system info file, and the save when it exists.
  [[nodiscard]] std::filesystem::path export_bundle(
      const std::filesystem::path &save_path);

 private:
  std::string session_id_;
  std::filesystem::path log_directory_, log_path_, system_info_path_,
      support_directory_;
};

} // namespace stellar::native_support
