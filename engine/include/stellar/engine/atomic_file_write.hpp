#pragma once

#include <cstddef>
#include <filesystem>
#include <span>
#include <functional>
#include <string_view>
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

using AtomicTextSink = std::function<void(std::string_view)>;
using AtomicTextProducer = std::function<void(const AtomicTextSink&)>;
// Producer failure removes the owned temporary and leaves destination/backup
// untouched. Serialization streams through a bounded 64 KiB write buffer.
void write_file_atomically_stream(const std::filesystem::path&,const AtomicTextProducer&,
                                  AtomicFileWriteOptions options = {});

// On Windows, writes an exclusively owned sibling temporary file, flushes its
// complete contents to the device, and then moves or replaces the destination.
// Other platforms report operation_not_supported rather than weakening the
// durability contract.
void write_file_atomically(const std::filesystem::path &primary,
                           std::span<const std::byte> bytes,
                           AtomicFileWriteOptions options = {});

} // namespace stellar::engine
