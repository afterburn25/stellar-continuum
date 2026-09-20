#pragma once
#include "stellar/installer/maintenance.hpp"
#include <fstream>
namespace stellar::installer {
class WindowsPlatform final: public Platform {
public:
  // A nonempty isolated root redirects registration, shortcuts, logs and the
  // pending journal pointer for OS integration tests; production passes none.
  explicit WindowsPlatform(fs::path isolated_test_root={});
  std::optional<Registration> installed() override;
  Json snapshot() override;
  void publish(const Registration&,const Release&,std::uint64_t) override;
  void remove_registration(const Registration&) override;
  void restore(const Json&) override;
  void check_running(const fs::path&) override;
  void log(std::string_view) override;
  Json record_backup() override;
  std::optional<fs::path> pending() override;
  void set_pending(const std::optional<fs::path>&) override;
  std::optional<fs::path> serialization_key() override {return pending_file_.parent_path();}
  const fs::path& log_path() const {return log_path_;}
private:
  fs::path log_path_;std::ofstream log_;
  std::wstring registry_key_;fs::path shortcut_root_,pending_file_;
};
fs::path local_data();
fs::path default_install_directory();
std::wstring widen(std::string_view);
std::string narrow(std::wstring_view);
}
