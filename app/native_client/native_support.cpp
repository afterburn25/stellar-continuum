#include "native_support.hpp"

#include <algorithm>
#include <array>
#include <chrono>
#include <cstdint>
#include <fstream>
#include <random>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

namespace stellar::native_support {
namespace {

[[nodiscard]] std::string make_session_id() {
  std::random_device source;
  std::mt19937_64 generator((static_cast<std::uint64_t>(source()) << 32) |
                            source());
  std::ostringstream out;
  out << std::uppercase << std::hex;
  for (int index = 0; index < 12; ++index)
    out << static_cast<unsigned>(generator() & 0xfu);
  return out.str();
}

[[nodiscard]] std::string iso_timestamp(
    const std::chrono::system_clock::time_point moment) {
  const auto time = std::chrono::system_clock::to_time_t(moment);
  std::tm fields{};
  gmtime_s(&fields, &time);
  std::array<char, 40> text{};
  std::strftime(text.data(), text.size(), "%Y-%m-%dT%H:%M:%SZ", &fields);
  return text.data();
}

[[nodiscard]] std::string bundle_timestamp() {
  const auto time =
      std::chrono::system_clock::to_time_t(std::chrono::system_clock::now());
  std::tm fields{};
  gmtime_s(&fields, &time);
  std::array<char, 24> text{};
  std::strftime(text.data(), text.size(), "%Y%m%d-%H%M%S", &fields);
  return text.data();
}

[[nodiscard]] std::uint32_t dos_timestamp() {
  const auto time =
      std::chrono::system_clock::to_time_t(std::chrono::system_clock::now());
  std::tm fields{};
  gmtime_s(&fields, &time);
  const auto year = static_cast<std::uint32_t>(fields.tm_year + 1900);
  return ((std::max(year, 1980u) - 1980u) << 25) |
         (static_cast<std::uint32_t>(fields.tm_mon + 1) << 21) |
         (static_cast<std::uint32_t>(fields.tm_mday) << 16) |
         (static_cast<std::uint32_t>(fields.tm_hour) << 11) |
         (static_cast<std::uint32_t>(fields.tm_min) << 5) |
         (static_cast<std::uint32_t>(fields.tm_sec) >> 1);
}

class Crc32 final {
 public:
  Crc32() {
    for (std::uint32_t index = 0; index < 256; ++index) {
      std::uint32_t value = index;
      for (int bit = 0; bit < 8; ++bit)
        value = (value & 1u) ? (value >> 1) ^ 0xedb88320u : value >> 1;
      table_[index] = value;
    }
  }
  [[nodiscard]] std::uint32_t of(std::string_view data) const noexcept {
    std::uint32_t value = 0xffffffffu;
    for (const unsigned char byte : data)
      value = (value >> 8) ^ table_[(value ^ byte) & 0xffu];
    return value ^ 0xffffffffu;
  }

 private:
  std::array<std::uint32_t, 256> table_{};
};

void put_u16(std::string &out, std::uint16_t value) {
  out.push_back(static_cast<char>(value & 0xffu));
  out.push_back(static_cast<char>((value >> 8) & 0xffu));
}
void put_u32(std::string &out, std::uint32_t value) {
  for (int shift = 0; shift < 32; shift += 8)
    out.push_back(static_cast<char>((value >> shift) & 0xffu));
}

[[nodiscard]] std::string read_file(const std::filesystem::path &path) {
  std::ifstream input(path, std::ios::binary);
  if (!input)
    throw std::runtime_error("Support bundle could not read " +
                             path.filename().string());
  std::ostringstream buffer;
  buffer << input.rdbuf();
  return buffer.str();
}

// Store-format (uncompressed) ZIP — sufficient for diagnostics and fully
// readable by every archiver. Entry names carry the UTF-8 flag like the
// reference's ZipArchive output.
void write_store_zip(const std::filesystem::path &destination,
                     const std::vector<std::pair<std::string, std::string>>
                         &entries) {
  const Crc32 crc;
  const std::uint32_t stamp = dos_timestamp();
  std::string local, central;
  for (const auto &[name, data] : entries) {
    const auto checksum = crc.of(data);
    const auto size = static_cast<std::uint32_t>(data.size());
    const std::uint32_t offset = static_cast<std::uint32_t>(local.size());
    local += "PK\x03\x04";
    put_u16(local, 20);
    put_u16(local, 0x0800u);
    put_u16(local, 0);
    put_u32(local, stamp);
    put_u32(local, checksum);
    put_u32(local, size);
    put_u32(local, size);
    put_u16(local, static_cast<std::uint16_t>(name.size()));
    put_u16(local, 0);
    local += name;
    local += data;

    central += "PK\x01\x02";
    put_u16(central, 20);
    put_u16(central, 20);
    put_u16(central, 0x0800u);
    put_u16(central, 0);
    put_u32(central, stamp);
    put_u32(central, checksum);
    put_u32(central, size);
    put_u32(central, size);
    put_u16(central, static_cast<std::uint16_t>(name.size()));
    // extra length, comment length, disk start, internal attributes
    for (int field = 0; field < 4; ++field) put_u16(central, 0);
    put_u32(central, 0); // external attributes
    put_u32(central, offset);
    central += name;
  }
  const std::uint32_t central_offset = static_cast<std::uint32_t>(local.size());
  std::string trailer;
  trailer += "PK\x05\x06";
  put_u16(trailer, 0);
  put_u16(trailer, 0);
  put_u16(trailer, static_cast<std::uint16_t>(entries.size()));
  put_u16(trailer, static_cast<std::uint16_t>(entries.size()));
  put_u32(trailer, static_cast<std::uint32_t>(central.size()));
  put_u32(trailer, central_offset);
  put_u16(trailer, 0);

  std::ofstream output(destination,
                       std::ios::binary | std::ios::trunc);
  if (!output)
    throw std::runtime_error("Support bundle could not create " +
                             destination.filename().string());
  output << local << central << trailer;
  output.flush();
  if (!output)
    throw std::runtime_error("Support bundle could not write " +
                             destination.filename().string());
}

} // namespace

NativeSupportLog::NativeSupportLog(std::filesystem::path user_root,
                                   SystemInfo info)
    : session_id_(make_session_id()),
      log_directory_(std::move(user_root) / "logs"),
      support_directory_(log_directory_.parent_path() / "support") {
  std::filesystem::create_directories(log_directory_);
  std::filesystem::create_directories(support_directory_);
  log_path_ = log_directory_ / ("game-" + session_id_ + ".log");
  system_info_path_ = log_directory_ / ("system-" + session_id_ + ".txt");

  std::ostringstream system;
  system << "Session=" << session_id_ << "\n"
         << "GameVersion=" << info.game_version << "\n"
         << "Runtime=native\n"
         << "OS=" << info.platform << "\n"
         << "CPUThreads=" << info.cpu_cores << "\n"
         << "SystemRAMMiB=" << info.system_ram_mb << "\n"
         << "GPU=" << info.gpu_driver << "\n"
         << "DisplayCount=" << info.display_count << "\n";
  std::ofstream(system_info_path_, std::ios::binary | std::ios::trunc)
      << system.str();
  log("session", "Session " + session_id_ + " started.");
}

void NativeSupportLog::log(std::string_view category,
                           std::string_view message) {
  std::ofstream output(log_path_, std::ios::binary | std::ios::app);
  if (!output) return;
  output << iso_timestamp(std::chrono::system_clock::now()) << " ["
         << session_id_ << "] [" << category << "] " << message << '\n';
}

std::filesystem::path NativeSupportLog::export_bundle(
    const std::filesystem::path &save_path) {
  std::filesystem::create_directories(support_directory_);
  const auto destination = support_directory_ /
      ("support-" + session_id_ + "-" + bundle_timestamp() + ".zip");
  std::vector<std::pair<std::string, std::string>> entries;
  for (const auto &source : {log_path_, system_info_path_, save_path}) {
    if (source.empty()) continue;
    std::error_code error;
    if (!std::filesystem::is_regular_file(source, error) || error) continue;
    entries.emplace_back(source.filename().string(), read_file(source));
  }
  write_store_zip(destination, entries);
  log("support", "Support bundle exported to " + destination.string());
  return destination;
}

} // namespace stellar::native_support
