#include "native_support.hpp"

#include <chrono>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

using namespace stellar::native_support;

namespace {

void require(bool condition, const char *message) {
  if (!condition) throw std::runtime_error(message);
}

[[nodiscard]] std::string read_bytes(const std::filesystem::path &path) {
  std::ifstream input(path, std::ios::binary);
  std::ostringstream buffer;
  buffer << input.rdbuf();
  return buffer.str();
}

[[nodiscard]] std::uint32_t get_u32(const std::string &data,
                                    std::size_t offset) {
  return static_cast<std::uint32_t>(
             static_cast<unsigned char>(data[offset])) |
         (static_cast<std::uint32_t>(
              static_cast<unsigned char>(data[offset + 1])) << 8) |
         (static_cast<std::uint32_t>(
              static_cast<unsigned char>(data[offset + 2])) << 16) |
         (static_cast<std::uint32_t>(
              static_cast<unsigned char>(data[offset + 3])) << 24);
}
[[nodiscard]] std::uint16_t get_u16(const std::string &data,
                                    std::size_t offset) {
  return static_cast<std::uint16_t>(
      static_cast<unsigned char>(data[offset]) |
      (static_cast<std::uint16_t>(
           static_cast<unsigned char>(data[offset + 1])) << 8));
}

// Parses the produced store-format ZIP and returns {name, contents}.
[[nodiscard]] std::vector<std::pair<std::string, std::string>>
unzip(const std::string &bundle) {
  const std::string eocd_signature{"PK\x05\x06", 4};
  const auto eocd = bundle.rfind(eocd_signature);
  require(eocd != std::string::npos, "Bundle has no end-of-central-directory.");
  const auto entries = get_u16(bundle, eocd + 10);
  const auto central_size = get_u32(bundle, eocd + 12);
  const auto central_offset = get_u32(bundle, eocd + 16);
  require(central_offset + central_size == eocd,
          "Bundle central directory does not abut the end record.");

  std::vector<std::pair<std::string, std::string>> result;
  std::size_t cursor = central_offset;
  for (int index = 0; index < entries; ++index) {
    require(bundle.compare(cursor, 4, "PK\x01\x02", 4) == 0,
            "Central directory entry signature is missing.");
    const auto checksum = get_u32(bundle, cursor + 16);
    const auto size = get_u32(bundle, cursor + 20);
    const auto stored = get_u32(bundle, cursor + 24);
    require(size == stored, "Bundle entry must use store (uncompressed) form.");
    const auto name_length = get_u16(bundle, cursor + 28);
    const auto name =
        bundle.substr(cursor + 46, name_length);
    const auto local_offset = get_u32(bundle, cursor + 42);
    require(bundle.compare(local_offset, 4, "PK\x03\x04", 4) == 0,
            "Local file header signature is missing.");
    const auto local_name_length = get_u16(bundle, local_offset + 26);
    const auto local_extra_length = get_u16(bundle, local_offset + 28);
    const auto contents =
        bundle.substr(local_offset + 30 + local_name_length +
                          local_extra_length,
                      size);
    // Recompute CRC32 over the stored bytes and compare to the directory.
    std::uint32_t value = 0xffffffffu;
    static std::uint32_t table[256];
    static bool initialized = false;
    if (!initialized) {
      for (std::uint32_t n = 0; n < 256; ++n) {
        std::uint32_t c = n;
        for (int bit = 0; bit < 8; ++bit)
          c = (c & 1u) ? (c >> 1) ^ 0xedb88320u : c >> 1;
        table[n] = c;
      }
      initialized = true;
    }
    for (const unsigned char byte : contents)
      value = (value >> 8) ^ table[(value ^ byte) & 0xffu];
    require((value ^ 0xffffffffu) == checksum,
            "Bundle entry CRC32 does not match its contents.");
    result.emplace_back(name, contents);
    cursor += 46 + name_length;
  }
  require(cursor == eocd, "Central directory length is inconsistent.");
  return result;
}

} // namespace

int main() try {
  const auto root = std::filesystem::temp_directory_path() /
                    ("stellar-native-support-" + std::to_string(
                        std::chrono::steady_clock::now()
                            .time_since_epoch()
                            .count()));
  std::filesystem::create_directories(root);

  NativeSupportLog log(root, {"0.1.58-test", "Windows", "vulkan", 16, 32768, 1});
  require(log.session_id().size() == 12,
          "Session id must be 12 hex characters like the reference.");
  require(std::filesystem::is_regular_file(log.system_info_path()),
          "System info file was not written on startup.");
  const auto info = read_bytes(log.system_info_path());
  require(info.find("Session=" + log.session_id()) != std::string::npos &&
              info.find("Runtime=native") != std::string::npos &&
              info.find("GPU=vulkan") != std::string::npos,
          "System info file is missing expected fields.");

  log.log("test", "first line");
  log.log("test", "second line");
  const auto log_text = read_bytes(log.log_path());
  require(log_text.find("Session " + log.session_id() + " started") !=
              std::string::npos,
          "Session start line missing from the log.");
  require(log_text.find("[test] second line") != std::string::npos,
          "Appended log line missing.");

  const auto save = root / "campaign.player17.json";
  {
    std::ofstream(save) << "{\"FormatVersion\":17}";
  }
  const auto bundle_path = log.export_bundle(save);
  require(std::filesystem::is_regular_file(bundle_path),
          "Export did not create the bundle.");
  require(bundle_path.filename().string().find("support-" +
                                               log.session_id()) == 0,
          "Bundle filename does not carry the session id.");

  const auto entries = unzip(read_bytes(bundle_path));
  require(entries.size() == 3,
          "Bundle must hold the log, the system info, and the save.");
  bool saw_log = false, saw_info = false, saw_save = false;
  for (const auto &[name, contents] : entries) {
    if (name == log.log_path().filename().string() &&
        contents.find("second line") != std::string::npos)
      saw_log = true;
    if (name == log.system_info_path().filename().string() &&
        contents.find("GPU=vulkan") != std::string::npos)
      saw_info = true;
    if (name == "campaign.player17.json" &&
        contents == "{\"FormatVersion\":17}")
      saw_save = true;
  }
  require(saw_log && saw_info && saw_save,
          "Bundle is missing one of the reference's three entries.");

  const auto without_save =
      log.export_bundle(root / "absent.player17.json");
  require(unzip(read_bytes(without_save)).size() == 2,
          "Bundle without a save must still hold log and system info.");

  std::filesystem::remove_all(root);
  std::cout << "Native support log and bundle tests passed\n";
  return 0;
} catch (const std::exception &error) {
  std::cerr << error.what() << '\n';
  return 1;
}
