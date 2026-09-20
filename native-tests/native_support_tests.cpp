#include "native_support.hpp"
#include "diagnostic_zip_test_reader.hpp"

#include <chrono>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <map>
#include <stdexcept>
#include <string>

using stellar::native_support::SupportBundleRequest;
using stellar::native_support::export_support_bundle;
namespace fs = std::filesystem;

namespace {
void require(bool value, const char* message) { if (!value) throw std::runtime_error(message); }
using diagnostic_zip_test::read;
using diagnostic_zip_test::write;
using diagnostic_zip_test::unzip;
fs::path scratch() { const auto root = fs::temp_directory_path() / ("stellar-native-support-" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count())); fs::create_directories(root); return root; }
void expect_throw(const SupportBundleRequest& request, const char* message) { try { (void)export_support_bundle(request); } catch (const std::exception&) { return; } throw std::runtime_error(message); }

void round_trip_and_unique_destinations(const fs::path& root) {
  const auto unicode_parent = root / fs::path(u8"保存"); fs::create_directories(unicode_parent);
  const auto save = unicode_parent / "campaign.player17.json";
  const std::string bytes{"\x00\x01binary\xff\n", 10}; write(save, bytes);
  const SupportBundleRequest request{root, save, "GPU=Vulkan", "line one\nline two\n"};
  const auto first = export_support_bundle(request), second = export_support_bundle(request);
  require(first != second && fs::is_regular_file(first) && fs::is_regular_file(second), "rapid exports overwrote an existing bundle");
  const auto entries = unzip(first);
  require(entries.size() == 3 && entries.at("session.log") == request.session_log && entries.at("campaign.player17.json") == bytes, "round trip did not preserve fixed entry bytes");
  require(entries.at("system.txt").find("SaveIncluded=yes") != std::string::npos, "included save lacks system marker");
  require(fs::is_regular_file(save) && read(save) == bytes, "export modified the source save");
}
void absent_invalid_and_bounded_inputs(const fs::path& root) {
  SupportBundleRequest missing{root, root / "absent.player17.json", "OS=test", "session"};
  const auto entries = unzip(export_support_bundle(missing));
  require(entries.size() == 2 && entries.at("system.txt").find("SaveIncluded=no") != std::string::npos, "missing save did not export explicit marker");
  const auto directory = root / "directory-save"; fs::create_directory(directory); missing.save_path = directory; expect_throw(missing, "nonregular save was accepted");
  missing.save_path.clear(); missing.session_log.assign(256u * 1024u + 1, 'x'); expect_throw(missing, "oversize session snapshot was accepted");
  missing.session_log = "session"; const auto large = root / "large.player17.json"; { std::ofstream out(large, std::ios::binary); out.seekp(64ll * 1024ll * 1024ll); out.put('x'); } missing.save_path = large; expect_throw(missing, "oversize save was accepted");
  SupportBundleRequest bad{root / "not-a-directory", {}, "info", "log"}; write(bad.user_root, "file"); expect_throw(bad, "invalid destination was accepted");
}

void reusable_archive_guards(const fs::path &root){
  using namespace stellar::engine;
  const std::vector<DiagnosticBundleEntry> valid{{"logs/events.jsonl","line\n"},{"empty.txt",{}}};
  const auto archive=write_diagnostic_bundle(root/"shared","report.zip",valid);
  require(unzip(archive).at("logs/events.jsonl")=="line\n","Shared ZIP nested entry failed CRC roundtrip.");
  const auto rejected=root/"rejected";
  const auto deny=[&](std::vector<DiagnosticBundleEntry> entries,DiagnosticBundleLimits limits=DiagnosticBundleLimits{},std::string filename="report.zip"){
    bool failed=false;try{(void)write_diagnostic_bundle(rejected,filename,entries,limits);}catch(const std::exception&){failed=true;}
    require(failed&&!fs::exists(rejected),"Invalid bundle wrote files before validating inputs.");
  };
  for(const char *name:{"../escape","/absolute","C:/drive","a\\b","a/../b","a//b","a/","nul.txt","COM1.log","trailing.","a./b"})deny({{name,"x"}});
  deny({{"FILE.txt","a"},{"file.txt","b"}});deny({{"a","x"},{"a/b","y"}});
  deny({{std::string("bad\0name",8),"x"}});deny(valid,{2,64,8});deny(valid,{64,2,8});deny(valid,{64,64,1});deny(valid,{},"../report.zip");
  const auto exact=write_diagnostic_bundle(root/"exact","report.zip",valid,{5,5,2});
  require(unzip(exact).size()==2,"Exact archive budget rejected or corrupted entries.");
  require(read_diagnostic_file(exact,1024)==read(exact),"Bounded diagnostic read altered bytes.");
  bool read_rejected=false;try{(void)read_diagnostic_file(exact,2);}catch(const std::exception&){read_rejected=true;}
  require(read_rejected,"Oversized diagnostic read accepted.");
  SupportBundleRequest extended{root,{},"developer","log",{{"latest.dev17.json","checkpoint"}},"Stellar-Continuum-Diagnostic-test.zip"};
  const auto contents=unzip(export_support_bundle(extended));
  require(contents.at("latest.dev17.json")=="checkpoint"&&!contents.contains("campaign.player17.json"),"Developer export mislabeled its checkpoint.");
}
} // namespace

int main() try {
  const auto root = scratch();
  round_trip_and_unique_destinations(root);
  reusable_archive_guards(root);
  absent_invalid_and_bounded_inputs(root);
  fs::remove_all(root);
  return 0;
} catch (const std::exception& error) {
  std::cerr << error.what() << '\n';
  return 1;
}
