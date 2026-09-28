#include <stellar/engine/save_integrity.hpp>

#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>

using namespace stellar::engine;

namespace {
int failures = 0;
void check(bool condition, const char *message) {
  if (!condition) {
    std::cerr << "FAIL: " << message << '\n';
    ++failures;
  }
}
} // namespace

int main() {
  const auto dir = std::filesystem::temp_directory_path() /
                   "stellar_integrity_tests";
  std::filesystem::remove_all(dir);
  std::filesystem::create_directories(dir);
  const auto save = dir / "campaign.json";

  // Write a save + sidecar.
  {
    std::ofstream out(save, std::ios::binary | std::ios::trunc);
    out << "{\"FormatVersion\":17}";
  }
  const std::string contents = "{\"FormatVersion\":17}";
  check(write_integrity_sidecar(
            save, std::as_bytes(std::span(contents.data(), contents.size()))),
        "sidecar write succeeds");
  check(std::filesystem::exists(integrity_sidecar_path(save)),
        "sidecar exists");

  // Matching content verifies.
  check(verify_integrity(save) == IntegrityStatus::Verified,
        "intact save verifies");

  // Bit-rot flips a byte.
  {
    std::ofstream out(save, std::ios::binary | std::ios::trunc);
    out << "{\"FormatVersion\":18}";
  }
  check(verify_integrity(save) == IntegrityStatus::Mismatch,
        "corrupted save detected");
  check(integrity_error(save).has_value(), "error text surfaces");

  // No sidecar -> tolerated (legacy saves).
  const auto legacy = dir / "legacy.json";
  {
    std::ofstream out(legacy, std::ios::binary | std::ios::trunc);
    out << "{}";
  }
  check(verify_integrity(legacy) == IntegrityStatus::SidecarAbsent,
        "missing sidecar tolerated");
  check(!integrity_error(legacy).has_value(), "no error for legacy save");

  // Missing save -> SaveMissing.
  check(verify_integrity(dir / "gone.json") == IntegrityStatus::SaveMissing,
        "missing save reported");

  // File-based sidecar write covers .bak copies.
  check(write_integrity_sidecar_for_file(legacy),
        "file-based sidecar write");
  check(verify_integrity(legacy) == IntegrityStatus::Verified,
        "file-hashed sidecar verifies");

  std::filesystem::remove_all(dir);
  if (failures == 0)
    std::cout << "Save integrity tests passed\n";
  return failures == 0 ? 0 : 1;
}
