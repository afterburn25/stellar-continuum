#pragma once
#include "stellar/engine/product_version.hpp"
#include <nlohmann/json.hpp>
#include <atomic>
#include <filesystem>
#include <functional>
#include <optional>
#include <map>
#include <string>
#include <vector>

namespace stellar::installer {
namespace fs=std::filesystem;
using Json=nlohmann::json;
using Version=engine::ProductVersion;
inline fs::path path_from_utf8(std::string_view text) {return fs::path(std::u8string(text.begin(),text.end()));}
inline constexpr char product_id[]="StellarContinuum";
struct File {std::string path,sha256,package;std::uint64_t bytes{};};
struct Release {
  Version version; std::string engine_version,build_id; std::vector<File> files;
  Json document;
  std::string base_version,base_build;
  std::vector<std::string> payload_paths;
  [[nodiscard]] bool includes_payload(const File&) const;
  static Release parse(const Json&);
};
enum class Mode { Install, Update, Repair, DowngradeBlocked };
struct Registration {fs::path root; std::string install_id,version,installed_at;bool desktop{},developer{};};
struct Progress {std::string phase,file;std::uint64_t completed{},total{};bool can_cancel{};};
using ProgressSink=std::function<void(const Progress&)>;
struct Plan {
  Mode mode{};std::vector<File> changed,unchanged,obsolete;
  std::uint64_t stage_bytes{},required_bytes{},installed_bytes{},unchanged_bytes{};
  std::map<std::string,std::string> local_reuse;
};
// OS integration is injected; automated transactions never touch the player's
// real registration or shortcuts. Snapshot restoration is part of rollback.
class Platform {
public:
  virtual ~Platform()=default;
  virtual std::optional<Registration> installed()=0;
  virtual Json snapshot()=0;
  virtual void publish(const Registration&,const Release&,std::uint64_t)=0;
  virtual void remove_registration(const Registration&)=0;
  virtual void restore(const Json&)=0;
  virtual void check_running(const fs::path&)=0;
  virtual void log(std::string_view)=0;
  virtual Json record_backup() {return nullptr;}
  virtual std::optional<fs::path> pending() {return {};}
  virtual void set_pending(const std::optional<fs::path>&) {}
  virtual std::optional<fs::path> serialization_key() {return {};}
};
struct Request {
  fs::path payload,root;Release release;bool desktop{},developer{};
  std::atomic_bool* cancel{};ProgressSink progress;
  // Deliberately library-only: no production command-line fault injection.
  std::function<void(std::string_view)> checkpoint;
};
std::string sha256_file(const fs::path&,const std::function<void(std::uint64_t)>& progress={});
Json read_json(const fs::path&);
void write_json(const fs::path&,const Json&);
void validate_relative_path(std::string_view);
void validate_tree_path(const fs::path& root,const fs::path& relative);
Mode determine_mode(const std::optional<Registration>&,const Release&);
std::string mode_name(Mode);
Plan make_plan(const Request&,Platform&);
void recover(const fs::path&,Platform&);
void execute(const Request&,Platform&);
void uninstall(const Registration&,Platform&,const ProgressSink& = {});
std::string new_id();
std::string timestamp();
}
