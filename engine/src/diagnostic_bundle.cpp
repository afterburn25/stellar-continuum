#include <stellar/engine/diagnostic_bundle.hpp>
#include <set>

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

namespace stellar::engine {
namespace {
constexpr int unique_directory_attempts = 32;

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
  const auto year = std::clamp(local.tm_year + 1900, 1980, 2107);
  return (static_cast<std::uint32_t>(year - 1980) << 25) |
         (static_cast<std::uint32_t>(local.tm_mon + 1) << 21) |
         (static_cast<std::uint32_t>(local.tm_mday) << 16) |
         (static_cast<std::uint32_t>(local.tm_hour) << 11) |
         (static_cast<std::uint32_t>(local.tm_min) << 5) |
         (static_cast<std::uint32_t>(local.tm_sec) >> 1);
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

void write_store_zip(const std::filesystem::path& destination, std::span<const DiagnosticBundleEntry> entries) {
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

[[nodiscard]] std::string read_diagnostic_file(const std::filesystem::path& path, std::size_t limit) {
  std::error_code error;
  if (std::filesystem::is_symlink(std::filesystem::symlink_status(path, error)) || error ||
      !std::filesystem::is_regular_file(path, error) || error)
    throw std::runtime_error("Diagnostic source must be a regular file, not a link.");
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


namespace {
std::string safe_name(std::string_view name) {
  if(name.empty()||name.size()>65535)throw std::invalid_argument("Invalid diagnostic archive entry name.");
  std::string folded(name);
  for(auto &c:folded){
    const auto b=static_cast<unsigned char>(c);
    if(b<32||b==127||c=='\\'||c==':'||c=='*'||c=='?'||c=='"'||c=='<'||c=='>'||c=='|')
      throw std::invalid_argument("Unsafe diagnostic archive entry path.");
    if(c>='A'&&c<='Z')c=static_cast<char>(c-'A'+'a');
  }
  std::size_t at=0;
  while(at<folded.size()){
    const auto end=folded.find('/',at);
    const auto part=std::string_view(folded).substr(at,end==std::string::npos?folded.size()-at:end-at);
    if(part.empty()||part=="."||part==".."||part.back()=='.'||part.back()==' ')
      throw std::invalid_argument("Unsafe diagnostic archive entry component.");
    const auto stem=part.substr(0,part.find('.'));
    if(stem=="con"||stem=="prn"||stem=="aux"||stem=="nul"||
       (stem.size()==4&&(stem.starts_with("com")||stem.starts_with("lpt"))&&stem[3]>='0'&&stem[3]<='9'))
      throw std::invalid_argument("Reserved diagnostic archive entry name.");
    if(end==std::string::npos)break;
    at=end+1;if(at==folded.size())throw std::invalid_argument("Archive entries must name files.");
  }
  return folded;
}
}
std::filesystem::path write_diagnostic_bundle(const std::filesystem::path &root,std::string_view filename,
    std::span<const DiagnosticBundleEntry> entries,DiagnosticBundleLimits limits){
  if(root.empty())throw std::invalid_argument("Diagnostic bundle root is empty.");
  const auto archive=safe_name(filename);
  if(archive.find('/')!=std::string::npos||!archive.ends_with(".zip"))throw std::invalid_argument("Expected a ZIP filename.");
  if(entries.empty()||entries.size()>limits.maximum_entries||entries.size()>65535)
    throw std::invalid_argument("Diagnostic entry count exceeds limits.");
  std::set<std::string> names;
  std::uint64_t zip_size=22,total=0;
  for(const auto &entry:entries){
    if(!names.insert(safe_name(entry.name)).second)throw std::invalid_argument("Duplicate diagnostic entry path.");
    if(entry.bytes.size()>limits.maximum_entry_bytes||entry.bytes.size()>limits.maximum_total_bytes-total)
      throw std::invalid_argument("Diagnostic bundle exceeds input size limits.");
    total+=entry.bytes.size();zip_size+=76+2*entry.name.size()+entry.bytes.size();
    if(zip_size>std::numeric_limits<std::uint32_t>::max())throw std::invalid_argument("Diagnostic bundle exceeds ZIP32 limits.");
  }
  // Prevent file/directory collisions on extraction, including differing case.
  for(const auto &name:names)for(auto slash=name.find('/');slash!=std::string::npos;slash=name.find('/',slash+1))
    if(names.contains(name.substr(0,slash)))throw std::invalid_argument("Conflicting diagnostic entry paths.");
  const auto directory=unique_bundle_directory(std::filesystem::absolute(root));
  const auto final=directory/std::string(filename),partial=directory/(std::string(filename)+".partial");
  try{write_store_zip(partial,entries);std::filesystem::rename(partial,final);}
  catch(...){std::error_code ignored;std::filesystem::remove(partial,ignored);std::filesystem::remove(directory,ignored);throw;}
  return final;
}
} // namespace stellar::engine
