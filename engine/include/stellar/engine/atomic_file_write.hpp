#pragma once

#include <cstddef>
#include <filesystem>
#include <span>
#include <stdexcept>
#include <system_error>

namespace stellar::engine {

enum class AtomicFileWriteOperation {
  validate_path,
  create_directory,
  create_temporary,
  write_temporary,
  flush_temporary,
  close_temporary,
  inspect_destination,
  move_new,
  replace_existing,
};

struct AtomicFileWriteOptions {
  bool preserve_existing_backup{};
};

class AtomicFileWriteError final : public std::system_error {
public:
  AtomicFileWriteError(std::error_code code, AtomicFileWriteOperation operation,
                       std::filesystem::path primary,
                       std::filesystem::path backup,
                       std::filesystem::path temporary,
                       bool recovery_file_retained);

  [[nodiscard]] AtomicFileWriteOperation operation() const noexcept;
  [[nodiscard]] const std::filesystem::path &primary_path() const noexcept;
  [[nodiscard]] const std::filesystem::path &backup_path() const noexcept;
  [[nodiscard]] const std::filesystem::path &temporary_path() const noexcept;
  [[nodiscard]] bool recovery_file_retained() const noexcept;

private:
  AtomicFileWriteOperation operation_{};
  std::filesystem::path primary_;
  std::filesystem::path backup_;
  std::filesystem::path temporary_;
  bool recovery_file_retained_{};
};

// On Windows, writes an exclusively owned sibling temporary file, flushes its
// complete contents to the device, and then moves or replaces the destination.
// Other platforms report operation_not_supported rather than weakening the
// durability contract.
void write_file_atomically(const std::filesystem::path &primary,
                           std::span<const std::byte> bytes,
                           AtomicFileWriteOptions options = {});

} // namespace stellar::engine
