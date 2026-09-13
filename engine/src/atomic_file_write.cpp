#include <stellar/engine/atomic_file_write.hpp>
#include <stellar/engine/detail/atomic_file_write_test.hpp>

#include <algorithm>
#include <array>
#include <cwctype>
#include <map>
#include <memory>
#include <mutex>
#include <string>

#if defined(_WIN32)
#define NOMINMAX
#include <Windows.h>
#include <objbase.h>
#pragma comment(lib, "Ole32.lib")
#endif

namespace stellar::engine {
namespace {

std::string operation_name(AtomicFileWriteOperation operation) {
  switch (operation) {
  case AtomicFileWriteOperation::validate_path: return "validate path";
  case AtomicFileWriteOperation::create_directory: return "create directory";
  case AtomicFileWriteOperation::create_temporary: return "create temporary";
  case AtomicFileWriteOperation::write_temporary: return "write temporary";
  case AtomicFileWriteOperation::flush_temporary: return "flush temporary";
  case AtomicFileWriteOperation::close_temporary: return "close temporary";
  case AtomicFileWriteOperation::inspect_destination:
    return "inspect destination";
  case AtomicFileWriteOperation::move_new: return "move new destination";
  case AtomicFileWriteOperation::replace_existing:
    return "replace existing destination";
  }
  return "unknown operation";
}

#if defined(_WIN32)

class UniqueHandle {
public:
  explicit UniqueHandle(HANDLE value = INVALID_HANDLE_VALUE) noexcept
      : value_(value) {}
  ~UniqueHandle() {
    if (valid()) CloseHandle(value_);
  }
  UniqueHandle(const UniqueHandle &) = delete;
  UniqueHandle &operator=(const UniqueHandle &) = delete;
  UniqueHandle(UniqueHandle &&other) noexcept : value_(other.release()) {}
  UniqueHandle &operator=(UniqueHandle &&other) noexcept {
    if (this == &other) return *this;
    if (valid()) CloseHandle(value_);
    value_ = other.release();
    return *this;
  }
  [[nodiscard]] bool valid() const noexcept {
    return value_ != INVALID_HANDLE_VALUE && value_ != nullptr;
  }
  [[nodiscard]] HANDLE get() const noexcept { return value_; }
  HANDLE release() noexcept {
    const auto result = value_;
    value_ = INVALID_HANDLE_VALUE;
    return result;
  }

private:
  HANDLE value_;
};

std::filesystem::path absolute_normal(const std::filesystem::path &path) {
  std::error_code error;
  auto result = std::filesystem::absolute(path, error);
  if (error)
    throw AtomicFileWriteError(error,
                               AtomicFileWriteOperation::validate_path,
                               path, {}, {}, false);
  return result.lexically_normal();
}

std::wstring extended_path(const std::filesystem::path &path) {
  auto value = path.native();
  if (value.starts_with(LR"(\\?\)")) return value;
  if (value.starts_with(LR"(\\)"))
    return LR"(\\?\UNC\)" + value.substr(2);
  return LR"(\\?\)" + value;
}

std::error_code windows_error(DWORD value = GetLastError()) {
  return {static_cast<int>(value), std::system_category()};
}

std::wstring guid_token() {
  GUID guid{};
  const auto result = CoCreateGuid(&guid);
  if (FAILED(result))
    throw std::system_error(static_cast<int>(result), std::system_category(),
                            "CoCreateGuid");
  std::array<wchar_t, 40> text{};
  if (StringFromGUID2(guid, text.data(), static_cast<int>(text.size())) == 0)
    throw std::runtime_error("StringFromGUID2 failed");
  std::wstring token;
  for (const wchar_t character : std::wstring_view(text.data()))
    if (character != L'{' && character != L'}' && character != L'-')
      token.push_back(static_cast<wchar_t>(std::towlower(character)));
  return token;
}

std::filesystem::path temporary_path(const std::filesystem::path &primary) {
  auto result = primary;
  result += L"." + guid_token() + L".tmp";
  return result;
}

bool file_exists(const std::filesystem::path &path, DWORD &error) {
  const auto attributes = GetFileAttributesW(extended_path(path).c_str());
  if (attributes != INVALID_FILE_ATTRIBUTES) {
    error = ERROR_SUCCESS;
    return true;
  }
  error = GetLastError();
  return false;
}

bool delete_owned(const std::filesystem::path &path) {
  if (path.empty()) return false;
  if (DeleteFileW(extended_path(path).c_str())) return false;
  const auto error = GetLastError();
  return error != ERROR_FILE_NOT_FOUND && error != ERROR_PATH_NOT_FOUND;
}

std::mutex lock_registry_mutex;
std::map<std::wstring, std::weak_ptr<std::mutex>> destination_locks;

std::wstring lower(std::wstring value) {
  std::ranges::transform(value, value.begin(), [](wchar_t character) {
    return static_cast<wchar_t>(std::towlower(character));
  });
  return value;
}

std::wstring identity_key(const std::filesystem::path &primary) {
  const auto parent = primary.parent_path();
  UniqueHandle directory(CreateFileW(
      extended_path(parent).c_str(), FILE_READ_ATTRIBUTES,
      FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr,
      OPEN_EXISTING, FILE_FLAG_BACKUP_SEMANTICS, nullptr));
  if (!directory.valid()) return lower(primary.native());
  std::wstring resolved(32768, L'\0');
  const auto length = GetFinalPathNameByHandleW(
      directory.get(), resolved.data(), static_cast<DWORD>(resolved.size()),
      FILE_NAME_NORMALIZED);
  if (length == 0 || length >= resolved.size()) return lower(primary.native());
  resolved.resize(length);
  return lower(resolved + L"\\" + primary.filename().native());
}

std::shared_ptr<std::mutex> destination_mutex(
    const std::filesystem::path &primary) {
  const auto key = identity_key(primary);
  std::lock_guard guard(lock_registry_mutex);
  auto &slot = destination_locks[key];
  auto result = slot.lock();
  if (!result) {
    result = std::make_shared<std::mutex>();
    slot = result;
  }
  return result;
}

#if defined(STELLAR_ATOMIC_FILE_WRITE_TESTING)
thread_local detail::AtomicFileWriteFault injected_fault =
    detail::AtomicFileWriteFault::none;

bool fault_is(detail::AtomicFileWriteFault fault) noexcept {
  if (injected_fault != fault) return false;
  injected_fault = detail::AtomicFileWriteFault::none;
  return true;
}
#endif

[[noreturn]] void fail(std::error_code error,
                       AtomicFileWriteOperation operation,
                       const std::filesystem::path &primary,
                       const std::filesystem::path &backup,
                       const std::filesystem::path &temporary,
                       bool retain_temporary) {
  bool retained = retain_temporary;
  try {
    DWORD existence_error = ERROR_SUCCESS;
    retained = retain_temporary
                   ? file_exists(temporary, existence_error)
                   : delete_owned(temporary);
  } catch (...) {
    // Cleanup is best effort during failure reporting. If path conversion or
    // allocation itself fails, retain the recovery location in the exception.
    retained = !temporary.empty();
  }
  throw AtomicFileWriteError(error, operation, primary, backup, temporary,
                             retained);
}

[[noreturn]] void fail_open(std::error_code error,
                            AtomicFileWriteOperation operation,
                            const std::filesystem::path &primary,
                            const std::filesystem::path &backup,
                            const std::filesystem::path &temporary,
                            UniqueHandle &handle) {
  if (handle.valid()) CloseHandle(handle.release());
  fail(error, operation, primary, backup, temporary, false);
}

#endif

} // namespace

AtomicFileWriteError::AtomicFileWriteError(
    std::error_code code, AtomicFileWriteOperation operation,
    std::filesystem::path primary, std::filesystem::path backup,
    std::filesystem::path temporary, bool recovery_file_retained)
    : std::system_error(code, operation_name(operation)), operation_(operation),
      primary_(std::move(primary)), backup_(std::move(backup)),
      temporary_(std::move(temporary)),
      recovery_file_retained_(recovery_file_retained) {}

AtomicFileWriteOperation AtomicFileWriteError::operation() const noexcept {
  return operation_;
}
const std::filesystem::path &AtomicFileWriteError::primary_path() const noexcept {
  return primary_;
}
const std::filesystem::path &AtomicFileWriteError::backup_path() const noexcept {
  return backup_;
}
const std::filesystem::path &AtomicFileWriteError::temporary_path() const noexcept {
  return temporary_;
}
bool AtomicFileWriteError::recovery_file_retained() const noexcept {
  return recovery_file_retained_;
}

void write_file_atomically(const std::filesystem::path &path,
                           std::span<const std::byte> bytes,
                           AtomicFileWriteOptions options) {
#if !defined(_WIN32)
  throw AtomicFileWriteError(
      std::make_error_code(std::errc::operation_not_supported),
      AtomicFileWriteOperation::validate_path, path, {}, {}, false);
#else
  if (path.empty())
    throw AtomicFileWriteError(
        std::make_error_code(std::errc::invalid_argument),
        AtomicFileWriteOperation::validate_path, path, {}, {}, false);
  if (path.native().find(L'\0') != std::wstring::npos)
    throw AtomicFileWriteError(
        std::make_error_code(std::errc::invalid_argument),
        AtomicFileWriteOperation::validate_path, path, {}, {}, false);
  const auto primary = absolute_normal(path);
  const auto backup = std::filesystem::path(primary.native() + L".bak");
  const auto parent = primary.parent_path();
  std::error_code directory_error;
  std::filesystem::create_directories(
      std::filesystem::path(extended_path(parent)), directory_error);
  if (directory_error)
    throw AtomicFileWriteError(directory_error,
                               AtomicFileWriteOperation::create_directory,
                               primary, backup, {}, false);

  const auto mutex = destination_mutex(primary);
  std::lock_guard destination_guard(*mutex);

  std::filesystem::path temporary;
  UniqueHandle handle;
  for (int attempt = 0; attempt < 32; ++attempt) {
    try {
      temporary = temporary_path(primary);
    } catch (const std::system_error &error) {
      throw AtomicFileWriteError(error.code(),
                                 AtomicFileWriteOperation::create_temporary,
                                 primary, backup, {}, false);
    } catch (const std::runtime_error &) {
      throw AtomicFileWriteError(
          std::make_error_code(std::errc::io_error),
          AtomicFileWriteOperation::create_temporary, primary, backup, {},
          false);
    }
    DWORD forced_create_error = ERROR_SUCCESS;
#if defined(STELLAR_ATOMIC_FILE_WRITE_TESTING)
    const bool collide_once =
        injected_fault == detail::AtomicFileWriteFault::temporary_collision_once;
    const bool collide_all = injected_fault ==
                             detail::AtomicFileWriteFault::temporary_collision_exhaustion;
    const bool deny =
        injected_fault == detail::AtomicFileWriteFault::temporary_access_denied;
    if (collide_once || collide_all || deny) {
      UniqueHandle unowned(CreateFileW(
          extended_path(temporary).c_str(), GENERIC_WRITE, 0, nullptr,
          CREATE_NEW, FILE_ATTRIBUTE_NORMAL, nullptr));
      if (unowned.valid()) {
        constexpr std::array marker{'o', 't', 'h', 'e', 'r'};
        DWORD marker_written = 0;
        WriteFile(unowned.get(), marker.data(),
                  static_cast<DWORD>(marker.size()), &marker_written, nullptr);
      }
      if (collide_once)
        injected_fault = detail::AtomicFileWriteFault::none;
      if (deny) {
        injected_fault = detail::AtomicFileWriteFault::none;
        forced_create_error = ERROR_ACCESS_DENIED;
      }
    }
#endif
    if (forced_create_error == ERROR_SUCCESS)
      handle = UniqueHandle(CreateFileW(extended_path(temporary).c_str(),
                                        GENERIC_WRITE, 0, nullptr, CREATE_NEW,
                                        FILE_ATTRIBUTE_NORMAL, nullptr));
    if (handle.valid()) break;
    const auto error = forced_create_error == ERROR_SUCCESS
                           ? GetLastError()
                           : forced_create_error;
    if (error != ERROR_FILE_EXISTS && error != ERROR_ALREADY_EXISTS)
      throw AtomicFileWriteError(
          windows_error(error), AtomicFileWriteOperation::create_temporary,
          primary, backup, temporary, false);
  }
  if (!handle.valid())
    throw AtomicFileWriteError(
        windows_error(ERROR_FILE_EXISTS),
        AtomicFileWriteOperation::create_temporary, primary, backup,
        temporary, false);

  std::size_t offset = 0;
  while (offset < bytes.size()) {
    auto remaining = bytes.size() - offset;
#if defined(STELLAR_ATOMIC_FILE_WRITE_TESTING)
    if (injected_fault == detail::AtomicFileWriteFault::partial_write)
      remaining = std::min(remaining, std::max<std::size_t>(1, remaining / 2));
#endif
    const auto count = static_cast<DWORD>(std::min<std::size_t>(
        remaining, static_cast<std::size_t>(MAXDWORD)));
    DWORD written = 0;
    BOOL write_succeeded = FALSE;
#if defined(STELLAR_ATOMIC_FILE_WRITE_TESTING)
    if (fault_is(detail::AtomicFileWriteFault::zero_write)) {
      write_succeeded = TRUE;
    } else
#endif
    {
      write_succeeded = WriteFile(handle.get(), bytes.data() + offset, count,
                                  &written, nullptr);
    }
    if (!write_succeeded)
      fail_open(windows_error(), AtomicFileWriteOperation::write_temporary,
                primary, backup, temporary, handle);
    if (written == 0)
      fail_open(windows_error(ERROR_WRITE_FAULT),
                AtomicFileWriteOperation::write_temporary, primary, backup,
                temporary, handle);
    offset += written;
#if defined(STELLAR_ATOMIC_FILE_WRITE_TESTING)
    if (fault_is(detail::AtomicFileWriteFault::partial_write))
      fail_open(windows_error(ERROR_WRITE_FAULT),
                AtomicFileWriteOperation::write_temporary, primary, backup,
                temporary, handle);
#endif
  }

#if defined(STELLAR_ATOMIC_FILE_WRITE_TESTING)
  if (fault_is(detail::AtomicFileWriteFault::flush))
    fail_open(windows_error(ERROR_WRITE_FAULT),
              AtomicFileWriteOperation::flush_temporary, primary, backup,
              temporary, handle);
#endif
  if (!FlushFileBuffers(handle.get()))
    fail_open(windows_error(), AtomicFileWriteOperation::flush_temporary,
              primary, backup, temporary, handle);
  const auto raw_handle = handle.release();
  if (!CloseHandle(raw_handle))
    fail(windows_error(), AtomicFileWriteOperation::close_temporary, primary,
         backup, temporary, false);

#if defined(STELLAR_ATOMIC_FILE_WRITE_TESTING)
  if (fault_is(detail::AtomicFileWriteFault::pre_replace))
    fail(windows_error(ERROR_OPERATION_ABORTED),
         AtomicFileWriteOperation::replace_existing, primary, backup,
         temporary, false);
#endif

  DWORD existence_error = ERROR_SUCCESS;
  const bool exists = file_exists(primary, existence_error);
  if (!exists && existence_error != ERROR_FILE_NOT_FOUND &&
      existence_error != ERROR_PATH_NOT_FOUND)
    fail(windows_error(existence_error),
         AtomicFileWriteOperation::inspect_destination, primary, backup,
         temporary, false);

  if (!exists) {
#if defined(STELLAR_ATOMIC_FILE_WRITE_TESTING)
    if (fault_is(detail::AtomicFileWriteFault::race_destination)) {
      UniqueHandle racing(CreateFileW(extended_path(primary).c_str(),
                                      GENERIC_WRITE, 0, nullptr, CREATE_NEW,
                                      FILE_ATTRIBUTE_NORMAL, nullptr));
      if (racing.valid()) {
        constexpr std::array race_bytes{'r', 'a', 'c', 'e'};
        DWORD written = 0;
        WriteFile(racing.get(), race_bytes.data(),
                  static_cast<DWORD>(race_bytes.size()), &written, nullptr);
        FlushFileBuffers(racing.get());
      }
    }
#endif
    if (!MoveFileExW(extended_path(temporary).c_str(),
                     extended_path(primary).c_str(), MOVEFILE_WRITE_THROUGH))
      fail(windows_error(), AtomicFileWriteOperation::move_new, primary,
           backup, temporary, false);
    return;
  }

#if defined(STELLAR_ATOMIC_FILE_WRITE_TESTING)
  if (fault_is(detail::AtomicFileWriteFault::ambiguous_replace))
    fail(windows_error(ERROR_UNABLE_TO_MOVE_REPLACEMENT_2),
         AtomicFileWriteOperation::replace_existing, primary, backup,
         temporary, true);
#endif

  // Keep owned storage alive for the duration of ReplaceFileW.
  const auto backup_native = extended_path(backup);
  if (!ReplaceFileW(extended_path(primary).c_str(),
                    extended_path(temporary).c_str(),
                    options.preserve_existing_backup
                        ? nullptr
                        : backup_native.c_str(),
                    REPLACEFILE_IGNORE_MERGE_ERRORS, nullptr, nullptr)) {
    const auto error = GetLastError();
    const bool ambiguous = error == ERROR_UNABLE_TO_MOVE_REPLACEMENT ||
                           error == ERROR_UNABLE_TO_MOVE_REPLACEMENT_2;
    fail(windows_error(error), AtomicFileWriteOperation::replace_existing,
         primary, backup, temporary, ambiguous);
  }
#endif
}

#if defined(_WIN32) && defined(STELLAR_ATOMIC_FILE_WRITE_TESTING)
namespace detail {
void set_atomic_file_write_fault(AtomicFileWriteFault fault) noexcept {
  injected_fault = fault;
}
} // namespace detail
#endif

} // namespace stellar::engine
