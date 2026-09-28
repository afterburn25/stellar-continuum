#pragma once
#include <filesystem>
#include <fstream>
#include <map>
#include <cstdint>
#include <string>
#include <stdexcept>
namespace diagnostic_zip_test {
namespace fs=std::filesystem;
inline void require_zip(bool condition,const char *message){if(!condition)throw std::runtime_error(message);}
inline std::string read(const fs::path& path) { std::ifstream input(path, std::ios::binary); return {std::istreambuf_iterator<char>(input), {}}; }
inline void write(const fs::path& path, const std::string& bytes) { std::ofstream output(path, std::ios::binary); output.write(bytes.data(), static_cast<std::streamsize>(bytes.size())); }
inline std::uint16_t u16(const std::string& value, std::size_t at) { require_zip(at + 2 <= value.size(), "truncated zip u16"); return static_cast<unsigned char>(value[at]) | static_cast<std::uint16_t>(static_cast<unsigned char>(value[at + 1]) << 8); }
inline std::uint32_t u32(const std::string& value, std::size_t at) { require_zip(at + 4 <= value.size(), "truncated zip u32"); return static_cast<unsigned char>(value[at]) | (static_cast<std::uint32_t>(static_cast<unsigned char>(value[at + 1])) << 8) | (static_cast<std::uint32_t>(static_cast<unsigned char>(value[at + 2])) << 16) | (static_cast<std::uint32_t>(static_cast<unsigned char>(value[at + 3])) << 24); }
inline std::uint32_t crc32(const std::string& data) { std::uint32_t value = 0xffffffffu; for (const unsigned char byte : data) { std::uint32_t x = (value ^ byte) & 0xffu; for (int bit = 0; bit < 8; ++bit) x = (x & 1u) ? (x >> 1) ^ 0xedb88320u : x >> 1; value = (value >> 8) ^ x; } return value ^ 0xffffffffu; }
inline std::map<std::string, std::string> unzip(const fs::path& path) {
  const auto zip = read(path); const auto end = zip.rfind(std::string("PK\x05\x06", 4)); require_zip(end != std::string::npos, "missing zip trailer");
  const auto count = u16(zip, end + 10); const auto central = u32(zip, end + 16); std::size_t cursor = central; std::map<std::string, std::string> entries;
  for (unsigned i = 0; i < count; ++i) { require_zip(zip.compare(cursor, 4, "PK\x01\x02", 4) == 0, "missing central entry"); const auto checksum = u32(zip, cursor + 16), size = u32(zip, cursor + 20), local = u32(zip, cursor + 42); const auto name_size = u16(zip, cursor + 28); const auto name = zip.substr(cursor + 46, name_size); require_zip(zip.compare(local, 4, "PK\x03\x04", 4) == 0, "missing local entry"); const auto local_name = u16(zip, local + 26), extra = u16(zip, local + 28); const auto bytes = zip.substr(local + 30 + local_name + extra, size); require_zip(bytes.size() == size && crc32(bytes) == checksum, "zip contents or crc mismatch"); entries.emplace(name, bytes); cursor += 46 + name_size; }
  return entries;
}
}
