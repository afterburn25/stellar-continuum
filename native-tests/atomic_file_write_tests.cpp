#include <stellar/engine/atomic_file_write.hpp>
#include <stellar/engine/detail/atomic_file_write_test.hpp>
#include <nlohmann/json.hpp>

#define NOMINMAX
#include <Windows.h>
#include <bcrypt.h>
#pragma comment(lib, "Bcrypt.lib")

#include <algorithm>
#include <array>
#include <atomic>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <optional>
#include <span>
#include <stdexcept>
#include <string>
#include <thread>
#include <typeinfo>
#include <vector>

namespace {
using Json = nlohmann::ordered_json;
using namespace stellar::engine;

void require(bool condition, std::string message) {
  if (!condition) throw std::runtime_error(std::move(message));
}

std::string display_path(const std::filesystem::path &path) {
  const auto value = path.u8string();
  return {reinterpret_cast<const char *>(value.data()), value.size()};
}

std::wstring extended_path(const std::filesystem::path &path) {
  auto absolute = std::filesystem::absolute(path).lexically_normal().native();
  if (absolute.starts_with(LR"(\\?\)")) return absolute;
  if (absolute.starts_with(LR"(\\)"))
    return LR"(\\?\UNC\)" + absolute.substr(2);
  return LR"(\\?\)" + absolute;
}

std::vector<std::byte> read_bytes(const std::filesystem::path &path) {
  const auto handle = CreateFileW(extended_path(path).c_str(), GENERIC_READ,
                                  FILE_SHARE_READ | FILE_SHARE_WRITE |
                                      FILE_SHARE_DELETE,
                                  nullptr, OPEN_EXISTING,
                                  FILE_ATTRIBUTE_NORMAL, nullptr);
  require(handle != INVALID_HANDLE_VALUE,
          "Cannot open '" + display_path(path) + "'.");
  LARGE_INTEGER size{};
  require(GetFileSizeEx(handle, &size) && size.QuadPart >= 0,
          "Cannot size '" + display_path(path) + "'.");
  std::vector<std::byte> result(static_cast<std::size_t>(size.QuadPart));
  std::size_t offset = 0;
  while (offset < result.size()) {
    const auto count = static_cast<DWORD>(std::min<std::size_t>(
        result.size() - offset, static_cast<std::size_t>(MAXDWORD)));
    DWORD read = 0;
    require(ReadFile(handle, result.data() + offset, count, &read, nullptr) &&
                read != 0,
            "Cannot read '" + display_path(path) + "'.");
    offset += read;
  }
  require(CloseHandle(handle), "Cannot close read handle.");
  return result;
}

bool file_exists(const std::filesystem::path &path) {
  return GetFileAttributesW(extended_path(path).c_str()) !=
         INVALID_FILE_ATTRIBUTES;
}

void direct_write(const std::filesystem::path &path,
                  std::span<const std::byte> bytes) {
  std::filesystem::create_directories(path.parent_path());
  const auto handle = CreateFileW(extended_path(path).c_str(), GENERIC_WRITE,
                                  0, nullptr, CREATE_ALWAYS,
                                  FILE_ATTRIBUTE_NORMAL, nullptr);
  require(handle != INVALID_HANDLE_VALUE, "direct create");
  DWORD written = 0;
  require(bytes.empty() ||
              (WriteFile(handle, bytes.data(), static_cast<DWORD>(bytes.size()),
                         &written, nullptr) &&
               written == bytes.size()),
          "direct write");
  require(CloseHandle(handle), "direct close");
}

std::vector<std::byte> byte_string(std::string_view value) {
  return {reinterpret_cast<const std::byte *>(value.data()),
          reinterpret_cast<const std::byte *>(value.data() + value.size())};
}

std::filesystem::path path_from_utf8(std::string_view value) {
  return std::filesystem::path(std::u8string(
      reinterpret_cast<const char8_t *>(value.data()), value.size()));
}

std::vector<std::byte> decode_base64(std::string_view value) {
  constexpr std::string_view alphabet =
      "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
  std::vector<std::byte> result;
  unsigned accumulator = 0;
  int bits = 0;
  for (const char character : value) {
    if (character == '=') break;
    const auto index = alphabet.find(character);
    require(index != std::string_view::npos, "invalid base64");
    accumulator = (accumulator << 6) | static_cast<unsigned>(index);
    bits += 6;
    if (bits >= 8) {
      bits -= 8;
      result.push_back(static_cast<std::byte>((accumulator >> bits) & 0xff));
    }
  }
  return result;
}

std::optional<std::vector<std::byte>> optional_bytes(const Json &value) {
  if (value.is_null()) return std::nullopt;
  return decode_base64(value.get<std::string>());
}

void check_optional_file(const std::filesystem::path &path,
                         const Json &expected, std::string label) {
  const auto wanted = optional_bytes(expected);
  require(file_exists(path) == wanted.has_value(), label + " presence");
  if (wanted) require(read_bytes(path) == *wanted, label + " bytes");
}

std::vector<std::string> temp_names(const std::filesystem::path &path) {
  std::vector<std::string> result;
  if (!std::filesystem::exists(path.parent_path())) return result;
  const auto prefix = path.filename().string() + ".";
  for (const auto &entry : std::filesystem::directory_iterator(path.parent_path())) {
    const auto name = entry.path().filename().string();
    if (name.starts_with(prefix) && name.ends_with(".tmp"))
      result.push_back(name);
  }
  std::ranges::sort(result);
  return result;
}

std::string sha256(std::span<const std::byte> bytes) {
  BCRYPT_ALG_HANDLE algorithm{};
  require(BCryptOpenAlgorithmProvider(&algorithm, BCRYPT_SHA256_ALGORITHM,
                                      nullptr, 0) == 0,
          "open SHA-256");
  DWORD object_size = 0;
  DWORD copied = 0;
  require(BCryptGetProperty(algorithm, BCRYPT_OBJECT_LENGTH,
                            reinterpret_cast<PUCHAR>(&object_size),
                            sizeof(object_size), &copied, 0) == 0,
          "SHA-256 object size");
  std::vector<UCHAR> object(object_size);
  BCRYPT_HASH_HANDLE handle{};
  require(BCryptCreateHash(algorithm, &handle, object.data(), object_size,
                           nullptr, 0, 0) == 0,
          "create SHA-256");
  require(bytes.empty() ||
              BCryptHashData(handle,
                             reinterpret_cast<PUCHAR>(
                                 const_cast<std::byte *>(bytes.data())),
                             static_cast<ULONG>(bytes.size()), 0) == 0,
          "hash bytes");
  std::array<UCHAR, 32> digest{};
  require(BCryptFinishHash(handle, digest.data(),
                           static_cast<ULONG>(digest.size()), 0) == 0,
          "finish SHA-256");
  BCryptDestroyHash(handle);
  BCryptCloseAlgorithmProvider(algorithm, 0);
  constexpr char digits[] = "0123456789ABCDEF";
  std::string result;
  for (const auto byte : digest) {
    result += digits[byte >> 4];
    result += digits[byte & 15];
  }
  return result;
}

std::vector<std::byte> file_bytes(const std::filesystem::path &path) {
  std::ifstream input(path, std::ios::binary);
  require(bool(input), "Cannot open '" + display_path(path) + "'.");
  std::string value{std::istreambuf_iterator<char>(input), {}};
  return byte_string(value);
}

void replay_source(const std::filesystem::path &source_root,
                   const std::filesystem::path &fixture_path,
                   const std::filesystem::path &scratch) {
  const auto fixture_bytes = file_bytes(fixture_path);
  require(sha256(fixture_bytes) ==
              "908E01AFBDF31E3B7E532A8A6A570815B4534313A038CBEF227AA9D31D1248D1",
          "fixture SHA-256");
  const auto fixture = Json::parse(
      reinterpret_cast<const char *>(fixture_bytes.data()),
      reinterpret_cast<const char *>(fixture_bytes.data() + fixture_bytes.size()));
  require(fixture.at("Schema") == "stellar.atomic-file-write.actual-source.v1",
          "fixture schema");
  require(fixture.at("RowCount") == 5 && fixture.at("Rows").size() == 5,
          "exact source row count");
  require(fixture.at("SourceHashBefore") == fixture.at("SourceHashAfter"),
          "source hash before/after");
  require(sha256(file_bytes(source_root /
                           fixture.at("SourceFile").get<std::string>())) ==
              fixture.at("SourceHashBefore").get<std::string>(),
          "actual source hash");

  std::filesystem::create_directories(scratch);
  std::size_t replayed = 0;
  for (const auto &row : fixture.at("Rows")) {
    const auto relative = row.at("RelativePath").get<std::string>();
    const auto path = scratch / path_from_utf8(relative);
    const auto backup = std::filesystem::path(path.native() + L".bak");
    const auto unrelated = std::filesystem::path(path.native() + L".unrelated.tmp");
    std::filesystem::create_directories(path.parent_path());
    const auto &before = row.at("Before");
    if (const auto value = optional_bytes(before.at("Primary")))
      direct_write(path, *value);
    if (const auto value = optional_bytes(before.at("Backup")))
      direct_write(backup, *value);
    if (const auto value = optional_bytes(before.at("Unrelated")))
      direct_write(unrelated, *value);
    check_optional_file(path, before.at("Primary"), "source before primary");
    check_optional_file(backup, before.at("Backup"), "source before backup");
    require(temp_names(path) == before.at("Temps").get<std::vector<std::string>>(),
            "source before temps");

    const auto text = row.at("Text").get<std::string>();
    write_file_atomically(path, byte_string(text),
                          {row.at("PreserveExistingBackup").get<bool>()});
    const auto &after = row.at("After");
    check_optional_file(path, after.at("Primary"), "source after primary");
    check_optional_file(backup, after.at("Backup"), "source after backup");
    check_optional_file(unrelated, after.at("Unrelated"), "source unrelated");
    require(temp_names(path) == after.at("Temps").get<std::vector<std::string>>(),
            "source after temps");
    ++replayed;
  }
  require(replayed == 5, "source replay accounting");
}

void expect_fault(const std::filesystem::path &path,
                  detail::AtomicFileWriteFault fault,
                  AtomicFileWriteOperation operation) {
  const auto old = byte_string("old");
  const auto backup = byte_string("backup");
  direct_write(path, old);
  direct_write(std::filesystem::path(path.native() + L".bak"), backup);
  detail::set_atomic_file_write_fault(fault);
  try {
    write_file_atomically(path, byte_string("replacement"));
    require(false, "fault should fail");
  } catch (const AtomicFileWriteError &error) {
    require(error.operation() == operation, "fault operation");
    require(error.primary_path() == std::filesystem::absolute(path).lexically_normal(),
            "fault primary path");
    require(error.backup_path() == std::filesystem::path(
                std::filesystem::absolute(path).lexically_normal().native() +
                L".bak"),
            "fault backup path");
    require(bool(error.code()), "fault error code");
    require(!error.recovery_file_retained(), "definite fault temp cleanup");
    require(!file_exists(error.temporary_path()), "fault temp absent");
  }
  require(read_bytes(path) == old, "fault primary unchanged");
  require(read_bytes(std::filesystem::path(path.native() + L".bak")) == backup,
          "fault backup unchanged");
}

void native_boundaries(const std::filesystem::path &scratch) {
  try {
    write_file_atomically({}, {});
    require(false, "empty path should fail");
  } catch (const AtomicFileWriteError &error) {
    require(error.operation() == AtomicFileWriteOperation::validate_path &&
                error.code() == std::errc::invalid_argument,
            "empty path category");
  }
  try {
    const std::wstring embedded_nul{L'b', L'a', L'd', L'\0', L't', L'a', L'i', L'l'};
    write_file_atomically(std::filesystem::path(embedded_nul),
                          byte_string("data"));
    require(false, "embedded NUL path should fail");
  } catch (const AtomicFileWriteError &error) {
    require(error.operation() == AtomicFileWriteOperation::validate_path &&
                error.code() == std::errc::invalid_argument,
            "embedded NUL path category");
  }

  const std::vector<std::byte> raw{std::byte{0}, std::byte{0xff}, std::byte{1},
                                   std::byte{'x'}, std::byte{0}};
  const auto unicode = scratch / L"space dir" / L"雪 Ω.bin";
  write_file_atomically(unicode, raw);
  require(read_bytes(unicode) == raw, "raw Unicode/NUL bytes");

  const auto unrelated = std::filesystem::path(unicode.native() + L".owned-by-other.tmp");
  direct_write(unrelated, byte_string("other"));
  write_file_atomically(unicode, byte_string("second"));
  require(read_bytes(unrelated) == byte_string("other"), "unrelated temp retained");

  const auto collision = scratch / "collision.bin";
  detail::set_atomic_file_write_fault(
      detail::AtomicFileWriteFault::temporary_collision_once);
  write_file_atomically(collision, byte_string("ours"));
  auto collision_temps = temp_names(collision);
  require(collision_temps.size() == 1 &&
              read_bytes(collision.parent_path() / collision_temps[0]) ==
                  byte_string("other"),
          "unowned collision retained");
  require(DeleteFileW(extended_path(collision.parent_path() /
                                    collision_temps[0]).c_str()),
          "collision test cleanup");

  const auto denied = scratch / "denied-temp.bin";
  detail::set_atomic_file_write_fault(
      detail::AtomicFileWriteFault::temporary_access_denied);
  std::filesystem::path denied_temp;
  try {
    write_file_atomically(denied, byte_string("ours"));
    require(false, "denied temp should fail");
  } catch (const AtomicFileWriteError &error) {
    require(error.operation() == AtomicFileWriteOperation::create_temporary &&
                error.code().value() == ERROR_ACCESS_DENIED &&
                !error.recovery_file_retained(),
            "denied temp category");
    denied_temp = error.temporary_path();
  }
  require(!file_exists(denied) && file_exists(denied_temp) &&
              read_bytes(denied_temp) == byte_string("other"),
          "denied unowned temp retained");
  require(DeleteFileW(extended_path(denied_temp).c_str()),
          "denied test cleanup");

  const auto exhausted = scratch / "exhausted-temp.bin";
  detail::set_atomic_file_write_fault(
      detail::AtomicFileWriteFault::temporary_collision_exhaustion);
  try {
    write_file_atomically(exhausted, byte_string("ours"));
    require(false, "temp collision exhaustion should fail");
  } catch (const AtomicFileWriteError &error) {
    require(error.operation() == AtomicFileWriteOperation::create_temporary &&
                error.code().value() == ERROR_FILE_EXISTS &&
                !error.recovery_file_retained(),
            "collision exhaustion category");
  }
  detail::set_atomic_file_write_fault(detail::AtomicFileWriteFault::none);
  const auto exhausted_temps = temp_names(exhausted);
  require(exhausted_temps.size() == 32 && !file_exists(exhausted),
          "all unowned collisions retained");
  for (const auto &name : exhausted_temps)
    require(DeleteFileW(
                extended_path(exhausted.parent_path() / name).c_str()),
            "exhaustion test cleanup");

  expect_fault(scratch / "fault-write.bin",
               detail::AtomicFileWriteFault::partial_write,
               AtomicFileWriteOperation::write_temporary);
  expect_fault(scratch / "fault-zero-write.bin",
               detail::AtomicFileWriteFault::zero_write,
               AtomicFileWriteOperation::write_temporary);
  expect_fault(scratch / "fault-flush.bin",
               detail::AtomicFileWriteFault::flush,
               AtomicFileWriteOperation::flush_temporary);
  expect_fault(scratch / "fault-pre-replace.bin",
               detail::AtomicFileWriteFault::pre_replace,
               AtomicFileWriteOperation::replace_existing);

  const auto ambiguous = scratch / "ambiguous-replace.bin";
  direct_write(ambiguous, byte_string("old"));
  direct_write(std::filesystem::path(ambiguous.native() + L".bak"),
               byte_string("known-backup"));
  detail::set_atomic_file_write_fault(
      detail::AtomicFileWriteFault::ambiguous_replace);
  std::filesystem::path retained;
  try {
    write_file_atomically(ambiguous, byte_string("recoverable"));
    require(false, "ambiguous replace should fail");
  } catch (const AtomicFileWriteError &error) {
    require(error.operation() == AtomicFileWriteOperation::replace_existing &&
                error.code().value() == ERROR_UNABLE_TO_MOVE_REPLACEMENT_2,
            "ambiguous replace category");
    require(error.recovery_file_retained() &&
                file_exists(error.temporary_path()),
            "ambiguous owned recovery retained");
    require(error.backup_path() ==
                std::filesystem::path(ambiguous.native() + L".bak"),
            "ambiguous backup path");
    retained = error.temporary_path();
  }
  require(read_bytes(ambiguous) == byte_string("old"),
          "injected ambiguous primary");
  require(read_bytes(std::filesystem::path(ambiguous.native() + L".bak")) ==
              byte_string("known-backup"),
          "injected ambiguous backup");
  require(read_bytes(retained) == byte_string("recoverable"),
          "injected ambiguous recovery bytes");
  require(DeleteFileW(extended_path(retained).c_str()),
          "test cleanup retained recovery");

  const auto race = scratch / "race-new.bin";
  detail::set_atomic_file_write_fault(
      detail::AtomicFileWriteFault::race_destination);
  try {
    write_file_atomically(race, byte_string("ours"));
    require(false, "new destination race should fail");
  } catch (const AtomicFileWriteError &error) {
    require(error.operation() == AtomicFileWriteOperation::move_new,
            "race operation");
    require(!error.recovery_file_retained() &&
                !file_exists(error.temporary_path()),
            "race temp cleanup");
  }
  require(read_bytes(race) == byte_string("race"), "racing primary preserved");

  const auto locked = scratch / "locked.bin";
  direct_write(locked, byte_string("locked-old"));
  const auto locked_handle = CreateFileW(extended_path(locked).c_str(),
                                         GENERIC_READ, 0, nullptr,
                                         OPEN_EXISTING,
                                         FILE_ATTRIBUTE_NORMAL, nullptr);
  require(locked_handle != INVALID_HANDLE_VALUE, "lock primary");
  try {
    write_file_atomically(locked, byte_string("new"));
    require(false, "locked replacement should fail");
  } catch (const AtomicFileWriteError &error) {
    require(error.operation() == AtomicFileWriteOperation::replace_existing,
            "locked operation");
    require(!error.recovery_file_retained() &&
                !file_exists(error.temporary_path()),
            "locked temp cleanup");
  }
  require(CloseHandle(locked_handle), "unlock primary");
  require(read_bytes(locked) == byte_string("locked-old"),
          "locked primary intact");

  const auto concurrent = scratch / "same-destination.bin";
  std::vector<std::vector<std::byte>> payloads;
  for (int index = 0; index < 8; ++index)
    payloads.emplace_back(200000, static_cast<std::byte>(index + 1));
  std::atomic<int> failures{};
  std::vector<std::thread> threads;
  for (const auto &payload : payloads)
    threads.emplace_back([&, payload] {
      try { write_file_atomically(concurrent, payload); }
      catch (...) { ++failures; }
    });
  for (auto &thread : threads) thread.join();
  require(failures == 0, "same-destination serialized success");
  const auto final = read_bytes(concurrent);
  const auto backup = read_bytes(
      std::filesystem::path(concurrent.native() + L".bak"));
  require(std::ranges::find(payloads, final) != payloads.end(),
          "same-destination complete primary");
  require(std::ranges::find(payloads, backup) != payloads.end(),
          "same-destination complete backup");
  require(temp_names(concurrent).empty(), "same-destination no owned temps");

  std::vector<std::thread> independent;
  for (int index = 0; index < 8; ++index)
    independent.emplace_back([&, index] {
      write_file_atomically(scratch / ("independent-" + std::to_string(index)),
                            payloads[index]);
    });
  for (auto &thread : independent) thread.join();
  for (int index = 0; index < 8; ++index)
    require(read_bytes(scratch / ("independent-" + std::to_string(index))) ==
                payloads[index],
            "independent destination");

  auto long_path = scratch / "long";
  for (int index = 0; index < 16; ++index)
    long_path /= L"segment-abcdefghijklmnop";
  long_path /= L"save.bin";
  write_file_atomically(long_path, raw);
  require(read_bytes(long_path) == raw &&
              std::filesystem::absolute(long_path).native().size() > MAX_PATH,
          "long path");

  std::array<wchar_t, 32768> short_buffer{};
  const auto short_length = GetShortPathNameW(
      extended_path(scratch).c_str(), short_buffer.data(),
      static_cast<DWORD>(short_buffer.size()));
  if (short_length > 0 && short_length < short_buffer.size()) {
    std::filesystem::path short_root(short_buffer.data());
    if (short_root != scratch) {
      const auto long_alias = scratch / "alias.bin";
      const auto short_alias = short_root / "alias.bin";
      std::thread first([&] {
        write_file_atomically(long_alias, payloads[0]);
      });
      std::thread second([&] {
        write_file_atomically(short_alias, payloads[1]);
      });
      first.join();
      second.join();
      require(std::ranges::find(payloads, read_bytes(long_alias)) !=
                  payloads.end(),
              "short alias primary");
      require(std::ranges::find(payloads,
                                read_bytes(std::filesystem::path(
                                    long_alias.native() + L".bak"))) !=
                  payloads.end(),
              "short alias backup");
    }
  }
}

} // namespace

int main(int argc, char **argv) {
  const auto cwd = std::filesystem::current_path();
  const std::string root_argument = argc > 1 ? argv[1] : "<missing>";
  const std::string fixture_argument = argc > 2 ? argv[2] : "<missing>";
  try {
    require(argc == 3, "Expected source root and fixture path.");
    const auto root = std::filesystem::absolute(root_argument);
    const auto fixture = std::filesystem::absolute(fixture_argument);
    const auto temp_root =
        std::filesystem::weakly_canonical(std::filesystem::temp_directory_path());
    std::filesystem::path normalized_scratch;
    bool owns_scratch = false;
    for (unsigned attempt = 0; attempt != 32; ++attempt) {
      const auto scratch = temp_root /
          ("stellar-atomic-086-" + std::to_string(GetCurrentProcessId()) +
           "-" + std::to_string(GetTickCount64()) + "-" +
           std::to_string(attempt));
      normalized_scratch = scratch.lexically_normal();
      if (CreateDirectoryW(extended_path(normalized_scratch).c_str(), nullptr)) {
        owns_scratch = true;
        break;
      }
      require(GetLastError() == ERROR_ALREADY_EXISTS,
              "Cannot create owned scratch directory");
    }
    require(owns_scratch, "Cannot reserve unique owned scratch directory");
    require(normalized_scratch.parent_path() == temp_root,
            "scratch containment");
    try {
      replay_source(root, fixture, normalized_scratch / "source");
      native_boundaries(normalized_scratch / "native");
    } catch (...) {
      std::error_code ignored;
      std::filesystem::remove_all(
          std::filesystem::path(extended_path(normalized_scratch)), ignored);
      throw;
    }
    std::error_code cleanup_error;
    std::filesystem::remove_all(
        std::filesystem::path(extended_path(normalized_scratch)), cleanup_error);
    require(!cleanup_error, "Cannot clean owned scratch directory");
    std::cout << "atomic file write parity: 5 source rows and native boundaries passed\n";
    return 0;
  } catch (const std::exception &error) {
    std::cerr << "atomic file write parity failure: " << typeid(error).name()
              << ": " << error.what() << '\n'
              << "cwd: " << cwd.string() << '\n'
              << "source root: "
              << (root_argument == "<missing>" ? root_argument
                  : std::filesystem::absolute(root_argument).string()) << '\n'
              << "fixture: "
              << (fixture_argument == "<missing>" ? fixture_argument
                  : std::filesystem::absolute(fixture_argument).string()) << '\n';
    return 1;
  }
}
