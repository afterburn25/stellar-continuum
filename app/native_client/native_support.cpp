#include "native_support.hpp"

#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <cstdint>
#include <ctime>
#include <fstream>
#include <limits>
#include <random>
#include <stdexcept>
#include <string_view>
#include <system_error>
#include <vector>

namespace stellar::native_support {
namespace {
constexpr std::size_t maximum_text_bytes = 256u * 1024u;
constexpr std::size_t maximum_save_bytes = 64u * 1024u * 1024u;
constexpr std::size_t maximum_bundle_input_bytes = 65u * 1024u * 1024u;
constexpr int unique_directory_attempts = 32;

struct ZipEntry final { std::string name, bytes; };
struct CentralEntry final { std::string_view name; std::uint32_t crc{}, size{}, offset{}; };

class Crc32 final {
 public:
  Crc32() {
    for (std::uint32_t index = 0; index < table_.size(); ++index) {
      std::uint32_t value = index;
      for (int bit = 0; bit < 8; ++bit)
        value = (value & 1u) ? (value >> 1) ^ 0xedb88320u : value >> 1;
      table_[index] = value;
    }
  }
  [[nodiscard]] std::uint32_t of(std::string_view bytes) const noexcept {
    std::uint32_t value = 0xffffffffu;
    for (const auto byte : bytes)
      value = (value >> 8) ^ table_[(value ^ static_cast<unsigned char>(byte)) & 0xffu];
    return value ^ 0xffffffffu;
  }
 private:
  std::array<std::uint32_t, 256> table_{};
};

void put_u16(std::ostream& output, std::uint16_t value) {
  const std::array<char, 2> bytes{static_cast<char>(value & 0xffu), static_cast<char>((value >> 8) & 0xffu)};
  output.write(bytes.data(), static_cast<std::streamsize>(bytes.size()));
}
void put_u32(std::ostream& output, std::uint32_t value) {
  const std::array<char, 4> bytes{static_cast<char>(value & 0xffu), static_cast<char>((value >> 8) & 0xffu),
                                  static_cast<char>((value >> 16) & 0xffu), static_cast<char>((value >> 24) & 0xffu)};
  output.write(bytes.data(), static_cast<std::streamsize>(bytes.size()));
}
[[nodiscard]] std::uint32_t dos_timestamp() {
  const auto now = std::chrono::system_clock::to_time_t(std::chrono::system_clock::now());
  std::tm local{};
#ifdef _WIN32
  localtime_s(&local, &now);
#else
  localtime_r(&now, &local);
#endif
  const auto year = std::max(1980, local.tm_year + 1900);
  return (static_cast<std::uint32_t>(year - 1980) << 25) |
         (static_cast<std::uint32_t>(local.tm_mon + 1) << 21) |
         (static_cast<std::uint32_t>(local.tm_mday) << 16) |
         (static_cast<std::uint32_t>(local.tm_hour) << 11) |
         (static_cast<std::uint32_t>(local.tm_min) << 5) |
         (static_cast<std::uint32_t>(local.tm_sec) >> 1);
}

[[nodiscard]] std::string read_regular_file(const std::filesystem::path& path, std::size_t limit) {
  std::error_code error;
  const auto size = std::filesystem::file_size(path, error);
  if (error || size > limit) throw std::runtime_error("Support bundle save is unreadable or exceeds its size limit.");
  std::ifstream input(path, std::ios::binary);
  if (!input) throw std::runtime_error("Support bundle could not open the save.");
  std::string bytes(static_cast<std::size_t>(size), '\0');
  if (size != 0) input.read(bytes.data(), static_cast<std::streamsize>(size));
  if (!input && !input.eof()) throw std::runtime_error("Support bundle could not read the save.");
  if (static_cast<std::uintmax_t>(input.gcount()) != size)
    throw std::runtime_error("Support bundle save changed while being read.");
  if (input.peek() != std::char_traits<char>::eof())
    throw std::runtime_error("Support bundle save changed while being read.");
  if (input.bad()) throw std::runtime_error("Support bundle could not finish reading the save.");
  return bytes;
}

[[nodiscard]] std::filesystem::path unique_bundle_directory(const std::filesystem::path& support_root) {
  std::error_code error;
  std::filesystem::create_directories(support_root, error);
  if (error) throw std::runtime_error("Support bundle directory could not be created.");
  static std::atomic<std::uint64_t> sequence{};
  std::random_device random;
  for (int attempt = 0; attempt < unique_directory_attempts; ++attempt) {
    const auto stamp = std::chrono::steady_clock::now().time_since_epoch().count();
    const auto token = (static_cast<std::uint64_t>(random()) << 32) ^ random() ^ sequence.fetch_add(1, std::memory_order_relaxed);
    const auto candidate = support_root / ("bundle-" + std::to_string(stamp) + "-" + std::to_string(token));
    error.clear();
    if (std::filesystem::create_directory(candidate, error)) return candidate;
    if (error && error != std::errc::file_exists)
      throw std::runtime_error("Support bundle directory could not be allocated.");
  }
  throw std::runtime_error("Support bundle could not allocate a unique destination.");
}

void write_store_zip(const std::filesystem::path& destination, const std::vector<ZipEntry>& entries) {
  if (entries.size() > std::numeric_limits<std::uint16_t>::max()) throw std::runtime_error("Support bundle has too many entries.");
  const Crc32 crc;
  const auto stamp = dos_timestamp();
  std::ofstream output(destination, std::ios::binary | std::ios::trunc);
  if (!output) throw std::runtime_error("Support bundle could not create its temporary file.");
  std::vector<CentralEntry> central;
  std::uint64_t offset{};
  for (const auto& entry : entries) {
    if (entry.name.size() > std::numeric_limits<std::uint16_t>::max() || entry.bytes.size() > std::numeric_limits<std::uint32_t>::max() || offset > std::numeric_limits<std::uint32_t>::max())
      throw std::runtime_error("Support bundle exceeds ZIP32 limits.");
    const auto size = static_cast<std::uint32_t>(entry.bytes.size());
    const auto checksum = crc.of(entry.bytes);
    output.write("PK\x03\x04", 4); put_u16(output, 20); put_u16(output, 0x0800u); put_u16(output, 0);
    put_u32(output, stamp); put_u32(output, checksum); put_u32(output, size); put_u32(output, size);
    put_u16(output, static_cast<std::uint16_t>(entry.name.size())); put_u16(output, 0);
    output.write(entry.name.data(), static_cast<std::streamsize>(entry.name.size()));
    output.write(entry.bytes.data(), static_cast<std::streamsize>(entry.bytes.size()));
    if (!output) throw std::runtime_error("Support bundle could not write its temporary file.");
    central.push_back({entry.name, checksum, size, static_cast<std::uint32_t>(offset)});
    offset += 30u + entry.name.size() + entry.bytes.size();
  }
  const auto central_offset = offset;
  for (const auto& entry : central) {
    output.write("PK\x01\x02", 4); put_u16(output, 20); put_u16(output, 20); put_u16(output, 0x0800u); put_u16(output, 0);
    put_u32(output, stamp); put_u32(output, entry.crc); put_u32(output, entry.size); put_u32(output, entry.size);
    put_u16(output, static_cast<std::uint16_t>(entry.name.size())); put_u16(output, 0); put_u16(output, 0); put_u16(output, 0); put_u16(output, 0);
    put_u32(output, 0); put_u32(output, entry.offset);
    output.write(entry.name.data(), static_cast<std::streamsize>(entry.name.size()));
    offset += 46u + entry.name.size();
  }
  const auto central_size = offset - central_offset;
  if (central_offset > std::numeric_limits<std::uint32_t>::max() || central_size > std::numeric_limits<std::uint32_t>::max()) throw std::runtime_error("Support bundle exceeds ZIP32 limits.");
  output.write("PK\x05\x06", 4); put_u16(output, 0); put_u16(output, 0); put_u16(output, static_cast<std::uint16_t>(central.size())); put_u16(output, static_cast<std::uint16_t>(central.size()));
  put_u32(output, static_cast<std::uint32_t>(central_size)); put_u32(output, static_cast<std::uint32_t>(central_offset)); put_u16(output, 0);
  output.flush();
  if (!output) throw std::runtime_error("Support bundle could not finish its temporary file.");
  output.close();
  if (!output) throw std::runtime_error("Support bundle could not close its temporary file.");
}
} // namespace

std::filesystem::path export_support_bundle(const SupportBundleRequest& request) {
  if (request.user_root.empty()) throw std::runtime_error("Support bundle user root is empty.");
  if (request.session_log.size() > maximum_text_bytes || request.system_info.size() > maximum_text_bytes)
    throw std::runtime_error("Support bundle text snapshot exceeds its size limit.");
  std::vector<ZipEntry> entries;
  entries.push_back({"session.log", request.session_log});
  std::error_code error;
  bool save_included = false;
  std::string save_bytes;
  if (!request.save_path.empty() && std::filesystem::exists(request.save_path, error)) {
    if (error || !std::filesystem::is_regular_file(request.save_path, error) || error)
      throw std::runtime_error("Support bundle save path is not a regular file.");
    save_bytes = read_regular_file(request.save_path, maximum_save_bytes);
    save_included = true;
  } else if (error) {
    throw std::runtime_error("Support bundle could not inspect the save path.");
  }
  const auto system = request.system_info + (request.system_info.empty() || request.system_info.back() == '\n' ? "" : "\n") + "SaveIncluded=" + (save_included ? "yes\n" : "no\n");
  if (system.size() > maximum_text_bytes || entries.front().bytes.size() + system.size() + save_bytes.size() > maximum_bundle_input_bytes)
    throw std::runtime_error("Support bundle exceeds its total size limit.");
  entries.push_back({"system.txt", system});
  if (save_included) entries.push_back({"campaign.player17.json", std::move(save_bytes)});
  const auto directory = unique_bundle_directory(std::filesystem::absolute(request.user_root) / "support");
  const auto partial = directory / "support.zip.partial";
  const auto final = directory / "support.zip";
  try {
    write_store_zip(partial, entries);
    std::filesystem::rename(partial, final, error);
    if (error) throw std::runtime_error("Support bundle could not be published.");
  } catch (...) {
    std::error_code ignored;
    std::filesystem::remove(partial, ignored);
    throw;
  }
  return final;
}
} // namespace stellar::native_support
