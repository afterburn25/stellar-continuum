#include <stellar/engine/save_integrity.hpp>

#include <stellar/engine/atomic_file_write.hpp>

#include <fstream>
#include <sstream>

namespace stellar::engine {

std::uint64_t save_digest(std::span<const std::byte> bytes) noexcept {
  std::uint64_t hash = 14695981039346656037ULL;
  for (const std::byte b : bytes) {
    hash ^= static_cast<std::uint64_t>(b);
    hash *= 1099511628211ULL;
  }
  return hash;
}

std::string integrity_sidecar_path(const std::filesystem::path &save_path) {
  auto copy = save_path;
  copy += ".integrity";
  return copy.string();
}

bool write_integrity_sidecar(const std::filesystem::path &save_path,
                             std::span<const std::byte> bytes) {
  std::ostringstream sidecar;
  sidecar << "fnv1a64:";
  sidecar << std::hex;
  sidecar << save_digest(bytes);
  const auto text = sidecar.str();
  try {
    write_file_atomically(integrity_sidecar_path(save_path),
                          std::as_bytes(std::span(text.data(), text.size())),
                          {});
    return true;
  } catch (...) {
    return false;
  }
}

bool write_integrity_sidecar_for_file(
    const std::filesystem::path &save_path) {
  std::ifstream input(save_path, std::ios::binary);
  if (!input)
    return false;
  std::ostringstream buffer;
  buffer << input.rdbuf();
  const auto bytes = buffer.str();
  return write_integrity_sidecar(
      save_path,
      std::as_bytes(std::span(bytes.data(), bytes.size())));
}

IntegrityStatus verify_integrity(const std::filesystem::path &save_path) {
  std::error_code ec;
  if (!std::filesystem::is_regular_file(save_path, ec))
    return IntegrityStatus::SaveMissing;
  const auto sidecar = integrity_sidecar_path(save_path);
  if (!std::filesystem::is_regular_file(sidecar, ec))
    return IntegrityStatus::SidecarAbsent;

  std::ifstream sidecar_in(sidecar, std::ios::binary);
  std::ostringstream sidecar_buf;
  sidecar_buf << sidecar_in.rdbuf();
  const auto sidecar_text = sidecar_buf.str();
  constexpr std::string_view prefix = "fnv1a64:";
  if (!sidecar_text.starts_with(std::string(prefix)))
    return IntegrityStatus::Mismatch;
  const auto expected =
      std::stoull(sidecar_text.substr(prefix.size()), nullptr, 16);

  std::ifstream input(save_path, std::ios::binary);
  if (!input)
    return IntegrityStatus::SaveMissing;
  std::ostringstream buffer;
  buffer << input.rdbuf();
  const auto bytes = buffer.str();
  const auto actual =
      save_digest(std::as_bytes(std::span(bytes.data(), bytes.size())));
  return actual == expected ? IntegrityStatus::Verified
                            : IntegrityStatus::Mismatch;
}

std::optional<std::string>
integrity_error(const std::filesystem::path &save_path) {
  switch (verify_integrity(save_path)) {
  case IntegrityStatus::Verified:
  case IntegrityStatus::SidecarAbsent:
    return std::nullopt;
  case IntegrityStatus::SaveMissing:
    return "save file is missing";
  case IntegrityStatus::Mismatch:
    return "save file failed its integrity check (content changed after "
           "write; possible corruption)";
  }
  return std::nullopt;
}

} // namespace stellar::engine
