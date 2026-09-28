#include <stellar/core/save_preview.hpp>
#include <stellar/engine/atomic_file_write.hpp>
#include <stellar/engine/save_history.hpp>
#include <stellar/engine/save_integrity.hpp>

#include <filesystem>
#include <fstream>
#include <iostream>
#include <span>
#include <sstream>
#include <string>

using namespace stellar;
namespace fs = std::filesystem;

namespace {
int failures = 0;
void check(bool condition, const char *message) {
  if (!condition) {
    std::cerr << "FAIL: " << message << '\n';
    ++failures;
  }
}
void write_file(const fs::path &path, std::string_view bytes) {
  std::ofstream out(path, std::ios::binary | std::ios::trunc);
  out.write(bytes.data(), static_cast<std::streamsize>(bytes.size()));
}
std::string read_file(const fs::path &path) {
  std::ifstream in(path, std::ios::binary);
  std::ostringstream buffer;
  buffer << in.rdbuf();
  return buffer.str();
}

// Mirrors write_prepared_player_campaign: rotate history only when a fresh
// .bak will be produced, then atomic write, then sidecars.
void save_generation(const fs::path &save, std::string_view generation,
                     bool preserve_backup = false) {
  const auto bytes =
      std::as_bytes(std::span(generation.data(), generation.size()));
  if (!preserve_backup) engine::rotate_save_history(save);
  engine::write_file_atomically(save, bytes, {preserve_backup});
  engine::write_integrity_sidecar(save, bytes);
  engine::write_history_sidecars(save);
}
} // namespace

int main() {
  const auto dir = fs::temp_directory_path() / "stellar_history_tests";
  fs::remove_all(dir);
  fs::create_directories(dir);
  const auto save = dir / "campaign.json";

  // Slot naming: 1 -> .bak, k -> .bak.k
  check(engine::history_slot_path(save, 1).string().ends_with(".bak"),
        "slot 1 is .bak");
  check(engine::history_slot_path(save, 3).string().ends_with(".bak.3"),
        "slot 3 is .bak.3");
  check(!engine::has_save_history(save), "no history before first backup");

  // Five real save generations through the actual atomic writer at depth 4.
  save_generation(save, "generation-1");
  check(!engine::has_save_history(save), "first write has no backup yet");
  save_generation(save, "generation-2");
  check(read_file(engine::history_slot_path(save, 1)) == "generation-1",
        ".bak holds the previous generation");
  save_generation(save, "generation-3");
  check(read_file(engine::history_slot_path(save, 1)) == "generation-2" &&
            read_file(engine::history_slot_path(save, 2)) == "generation-1",
        "rotation pushed the chain down");
  save_generation(save, "generation-4");
  save_generation(save, "generation-5");
  check(read_file(save) == "generation-5", "primary is the newest write");
  check(read_file(engine::history_slot_path(save, 1)) == "generation-4" &&
            read_file(engine::history_slot_path(save, 2)) == "generation-3" &&
            read_file(engine::history_slot_path(save, 3)) == "generation-2" &&
            read_file(engine::history_slot_path(save, 4)) == "generation-1",
        "all four history slots ordered newest to oldest");
  check(!fs::exists(engine::history_slot_path(save, 5)),
        "depth bound discards the oldest slot");

  // Every slot's rotated sidecar still verifies against its contents.
  for (std::size_t slot = 1; slot <= 4; ++slot)
    check(engine::verify_integrity(engine::history_slot_path(save, slot)) ==
              engine::IntegrityStatus::Verified,
          "rotated slot sidecar verifies");

  // A preserved-backup write does not rotate: .bak keeps the known-good copy.
  const auto preserved = read_file(engine::history_slot_path(save, 1));
  save_generation(save, "repaired", true);
  check(read_file(save) == "repaired" &&
            read_file(engine::history_slot_path(save, 1)) == preserved &&
            read_file(engine::history_slot_path(save, 2)) == "generation-3",
        "preserved backup write left the history chain untouched");

  // Preview reader — player-shaped flat document.
  const auto player = dir / "player.json";
  write_file(player,
             R"({"FormatVersion":17,"GalaxyFormatVersion":16,)"
             R"("GameVersion":"0.1.7-alpha",)"
             R"("SavedAtUtc":"2044-05-06T07:08:09+00:00",)"
             R"("SimulationDays":42.25})");
  const auto player_preview = core::read_player_campaign_preview(player);
  check(player_preview.has_value(), "player preview reads");
  check(player_preview && player_preview->format_version == 17 &&
            player_preview->galaxy_format_version == 16 &&
            player_preview->game_version == "0.1.7-alpha" &&
            player_preview->saved_at_utc == "2044-05-06T07:08:09+00:00" &&
            player_preview->simulation_days == 42.25 &&
            !player_preview->developer,
        "player preview fields extracted");
  check(player_preview &&
            player_preview->integrity == engine::IntegrityStatus::SidecarAbsent,
        "preview reports absent sidecar");

  // Preview reader — developer envelope with nested Campaign.
  const auto developer = dir / "developer.json";
  write_file(developer,
             R"({"DeveloperFormatVersion":1,"Mode":"Developer",)"
             R"("ToolsUsed":true,"Campaign":{"FormatVersion":17,)"
             R"("GameVersion":"0.1.8-dev","SimulationDays":7.5}})");
  const auto dev_preview = core::read_player_campaign_preview(developer);
  check(dev_preview && dev_preview->developer && dev_preview->tools_used &&
            dev_preview->format_version == 17 &&
            dev_preview->simulation_days == 7.5,
        "developer envelope preview extracts nested fields");

  // Preview tolerance: missing, malformed, and non-campaign files.
  check(!core::read_player_campaign_preview(dir / "gone.json"),
        "missing file -> no preview");
  const auto junk = dir / "junk.json";
  write_file(junk, "{ not json");
  check(!core::read_player_campaign_preview(junk), "malformed -> no preview");
  const auto foreign = dir / "foreign.json";
  write_file(foreign, R"({"Unrelated":true})");
  check(!core::read_player_campaign_preview(foreign),
        "non-campaign JSON -> no preview");

  fs::remove_all(dir);
  if (failures == 0)
    std::cout << "Save history and preview tests passed\n";
  return failures == 0 ? 0 : 1;
}
