#include "native_support.hpp"

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
std::string read(const fs::path& path) { std::ifstream input(path, std::ios::binary); return {std::istreambuf_iterator<char>(input), {}}; }
void write(const fs::path& path, const std::string& bytes) { std::ofstream output(path, std::ios::binary); output.write(bytes.data(), static_cast<std::streamsize>(bytes.size())); }
std::uint16_t u16(const std::string& value, std::size_t at) { require(at + 2 <= value.size(), "truncated zip u16"); return static_cast<unsigned char>(value[at]) | static_cast<std::uint16_t>(static_cast<unsigned char>(value[at + 1]) << 8); }
std::uint32_t u32(const std::string& value, std::size_t at) { require(at + 4 <= value.size(), "truncated zip u32"); return static_cast<unsigned char>(value[at]) | (static_cast<std::uint32_t>(static_cast<unsigned char>(value[at + 1])) << 8) | (static_cast<std::uint32_t>(static_cast<unsigned char>(value[at + 2])) << 16) | (static_cast<std::uint32_t>(static_cast<unsigned char>(value[at + 3])) << 24); }
std::uint32_t crc32(const std::string& data) { std::uint32_t value = 0xffffffffu; for (const unsigned char byte : data) { std::uint32_t x = (value ^ byte) & 0xffu; for (int bit = 0; bit < 8; ++bit) x = (x & 1u) ? (x >> 1) ^ 0xedb88320u : x >> 1; value = (value >> 8) ^ x; } return value ^ 0xffffffffu; }
std::map<std::string, std::string> unzip(const fs::path& path) {
  const auto zip = read(path); const auto end = zip.rfind(std::string("PK\x05\x06", 4)); require(end != std::string::npos, "missing zip trailer");
  const auto count = u16(zip, end + 10); const auto central = u32(zip, end + 16); std::size_t cursor = central; std::map<std::string, std::string> entries;
  for (unsigned i = 0; i < count; ++i) { require(zip.compare(cursor, 4, "PK\x01\x02", 4) == 0, "missing central entry"); const auto checksum = u32(zip, cursor + 16), size = u32(zip, cursor + 20), local = u32(zip, cursor + 42); const auto name_size = u16(zip, cursor + 28); const auto name = zip.substr(cursor + 46, name_size); require(zip.compare(local, 4, "PK\x03\x04", 4) == 0, "missing local entry"); const auto local_name = u16(zip, local + 26), extra = u16(zip, local + 28); const auto bytes = zip.substr(local + 30 + local_name + extra, size); require(bytes.size() == size && crc32(bytes) == checksum, "zip contents or crc mismatch"); entries.emplace(name, bytes); cursor += 46 + name_size; }
  return entries;
}
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
} // namespace

int main() try {
  const auto root = scratch();
  round_trip_and_unique_destinations(root);
  absent_invalid_and_bounded_inputs(root);
  fs::remove_all(root);
  return 0;
} catch (const std::exception& error) {
  std::cerr << error.what() << '\n';
  return 1;
}
