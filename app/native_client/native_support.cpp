#include "native_support.hpp"
#include <stellar/engine/runtime_diagnostics.hpp>
#include <stdexcept>
#include <system_error>
#include <vector>
namespace stellar::native_support {
namespace {
constexpr std::size_t maximum_text_bytes=256u*1024u;
constexpr std::size_t maximum_save_bytes=64u*1024u*1024u;
constexpr std::size_t maximum_bundle_input_bytes=65u*1024u*1024u;
constexpr std::size_t maximum_crash_report_bytes=256u*1024u;
// Newest crash report (.txt) from the diagnostics directory, so a support
// export carries the fault_site/stack/hang evidence that motivated it.
// Minidumps stay on disk — they are referenced by name, too large to bundle.
std::string latest_crash_report_text() {
  const auto directory=stellar::engine::RuntimeDiagnostics::diagnostics_directory();
  if(directory.empty())return{};
  // The live session's report is header-only until a fault writes it — only
  // completed reports from crashed/hung sessions carry evidence.
  const auto active_report=stellar::engine::RuntimeDiagnostics::active_report_path();
  std::error_code error;std::filesystem::path newest;std::filesystem::file_time_type stamp{};
  for(const auto& entry:std::filesystem::directory_iterator(directory,error)){
    if(!entry.is_regular_file(error)||entry.path().extension()!=".txt"||entry.file_size(error)==0)continue;
    if(!active_report.empty()&&entry.path()==active_report)continue;
    const auto written=entry.last_write_time(error);if(error)break;
    if(written>stamp){stamp=written;newest=entry.path();}
  }
  if(newest.empty()||error)return{};
  try{return stellar::engine::read_diagnostic_file(newest,maximum_crash_report_bytes);}catch(...){return{};}
}
}
std::filesystem::path export_support_bundle(const SupportBundleRequest& request) {
  if (request.user_root.empty()) throw std::runtime_error("Support bundle user root is empty.");
  if (request.session_log.size() > maximum_text_bytes || request.system_info.size() > maximum_text_bytes)
    throw std::runtime_error("Support bundle text snapshot exceeds its size limit.");
  std::vector<stellar::engine::DiagnosticBundleEntry> entries;
  entries.push_back({"session.log", request.session_log});
  std::error_code error;
  bool save_included = false;
  std::string save_bytes;
  if (!request.save_path.empty() && std::filesystem::exists(request.save_path, error)) {
    if (error || !std::filesystem::is_regular_file(request.save_path, error) || error)
      throw std::runtime_error("Support bundle save path is not a regular file.");
    save_bytes = stellar::engine::read_diagnostic_file(request.save_path, maximum_save_bytes);
    save_included = true;
  } else if (error) {
    throw std::runtime_error("Support bundle could not inspect the save path.");
  }
  const auto system = request.system_info + (request.system_info.empty() || request.system_info.back() == '\n' ? "" : "\n") + "SaveIncluded=" + (save_included ? "yes\n" : "no\n");
  if (system.size() > maximum_text_bytes || entries.front().bytes.size() + system.size() + save_bytes.size() > maximum_bundle_input_bytes)
    throw std::runtime_error("Support bundle exceeds its total size limit.");
  entries.push_back({"system.txt", system});
  if (save_included) entries.push_back({"campaign.player17.json", std::move(save_bytes)});
  if (auto crash=latest_crash_report_text();!crash.empty())entries.push_back({"crash-report.txt",std::move(crash)});
  entries.insert(entries.end(),request.additional_entries.begin(),request.additional_entries.end());
  return stellar::engine::write_diagnostic_bundle(std::filesystem::absolute(request.user_root)/"support",
      request.archive_name,entries);
}
} // namespace stellar::native_support
