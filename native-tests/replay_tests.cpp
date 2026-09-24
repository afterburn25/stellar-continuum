// Replay helpers: recorder/player round-trip, per-section checkpoint
// emission for divergence localization, and the expected-sequence
// verifier that names the diverging subsystem.

#include <stellar/engine/replay.hpp>

#include <nlohmann/json.hpp>

#include <iostream>
#include <vector>

namespace {

int failures = 0;
void check(bool condition, const char *message) {
  if (!condition) {
    ++failures;
    std::cerr << "FAIL: " << message << '\n';
  }
}

using namespace stellar::engine;

} // namespace

int main() {
  // Recorder/player round-trip preserves commands, checkpoints, labels.
  {
    ReplayRecorder recorder{ReplayHeader{42, "build", "1.0"}};
    recorder.record(10, "order", "{\"fleet\":7}");
    recorder.checkpoint(10, 1234, "save:World");
    const auto parsed = ReplayRecorder::parse(recorder.serialize());
    check(parsed.has_value(), "recording parses");
    check(parsed && parsed->commands().size() == 1 &&
              parsed->checkpoints().size() == 1 &&
              parsed->checkpoints()[0].label == "save:World" &&
              parsed->header().seed == 42,
          "round-trip keeps labels and header");
    ReplayPlayer player{&*parsed};
    check(player.commands_for(10).size() == 1 &&
              player.expected_checkpoint(10).value_or(0) == 1234,
          "player streams recorded entries");
  }

  // Occupancy census tracks command payloads, not just entry counts.
  {
    ReplayRecorder recorder;
    const auto empty = recorder.estimated_memory_bytes();
    recorder.record(1, "order",
                    std::string(2048, 'x')); // payload-heavy command
    recorder.checkpoint(1, 99, "save:World");
    check(recorder.estimated_memory_bytes() >= empty + 2048,
          "recorder census counts command payloads");
    check(ReplayRecorder::parse(recorder.serialize())
              ->estimated_memory_bytes() > 0,
          "parsed recorder reports a footprint");
  }

  // Section checkpoints emit top-level members plus object members one
  // level deep, in document order, with per-section hashes.
  nlohmann::ordered_json document{
      {"World", {{"Fleets", {1, 2}}, {"Colonies", {3}}}},
      {"Diplomacy", {{"Contacts", nlohmann::ordered_json::object()}}},
      {"EventHistory", nullptr},
      {"FormatVersion", 17}};
  {
    const auto sections =
        document_section_checkpoints(500, document, "save");
    const std::vector<std::string> want{
        "save:World",       "save:World.Fleets", "save:World.Colonies",
        "save:Diplomacy",   "save:Diplomacy.Contacts",
        "save:EventHistory", "save:FormatVersion"};
    check(sections.size() == want.size(), "section count");
    bool labels_ok = true, hashes_ok = true, ticks_ok = true;
    for (std::size_t i = 0; i < sections.size() && i < want.size(); ++i) {
      labels_ok &= sections[i].label == want[i];
      ticks_ok &= sections[i].tick == 500;
    }
    // Hashes equal the section's own document dump.
    hashes_ok &=
        sections[0].hash == fnv1a64(document.at("World").dump()) &&
        sections[1].hash ==
            fnv1a64(document.at("World").at("Fleets").dump()) &&
        sections[5].hash == fnv1a64(std::string("null"));
    check(labels_ok, "section labels in document order");
    check(ticks_ok, "section ticks");
    check(hashes_ok, "per-section hashes");
  }

  // Non-object documents yield no sections (defensive API contract).
  {
    const nlohmann::ordered_json array{1, 2, 3};
    check(document_section_checkpoints(0, array, "save").empty() &&
              document_section_checkpoints(0, 17, "save").empty(),
          "non-object documents produce no checkpoints");
  }

  // Verification: matching sequence advances the cursor; a section
  // mismatch names the label.
  const auto expected = document_section_checkpoints(500, document, "save");
  {
    auto same = document_section_checkpoints(500, document, "save");
    std::size_t cursor = 0;
    const auto result = verify_checkpoint_sequence(expected, cursor, same);
    check(result.divergence.empty() && result.verified == same.size() &&
              cursor == same.size(),
          "matching sequence verifies fully");
  }
  {
    auto changed = document_section_checkpoints(500, document, "save");
    changed[1].hash ^= 1; // Fleets diverges
    std::size_t cursor = 0;
    const auto result =
        verify_checkpoint_sequence(expected, cursor, changed);
    check(result.verified == 1 && cursor == 1 &&
              result.divergence.find("save:World.Fleets") !=
                  std::string::npos,
          "divergence names the subsystem");
  }
  {
    auto late = document_section_checkpoints(600, document, "save");
    std::size_t cursor = 0;
    const auto result = verify_checkpoint_sequence(expected, cursor, late);
    check(result.divergence.find("tick diverged") != std::string::npos,
          "tick mismatch reported");
  }
  {
    std::size_t cursor = expected.size();
    const auto result =
        verify_checkpoint_sequence(expected, cursor, expected);
    check(result.divergence.find("unrecorded") != std::string::npos,
          "overrun reported as unrecorded checkpoint");
  }

  // Leaf diff: identical documents produce no entries; a changed scalar
  // names its full path; member and array-length asymmetries report the
  // missing side as <absent>.
  {
    check(document_leaf_diff(document, document).empty(),
          "identical documents reported leaves");
    auto changed = document;
    changed["World"]["Fleets"][1] = 99;
    changed["Diplomacy"].erase("Contacts");
    changed["Extra"] = true;
    const auto leaves = document_leaf_diff(document, changed);
    check(leaves.size() == 3, "leaf diff counted the three divergences");
    bool paths_ok = false;
    for (const auto &leaf : leaves) {
      if (leaf.path == "World.Fleets[1]") {
        paths_ok = leaf.expected == "2" && leaf.actual == "99";
      }
    }
    check(paths_ok, "leaf diff named the changed scalar with both values");
    check(leaves[1].path == "Diplomacy.Contacts" &&
              leaves[1].actual == "<absent>",
          "missing member reports <absent> on the actual side");
    check(leaves[2].path == "Extra" && leaves[2].expected == "<absent>",
          "extra member reports <absent> on the expected side");
    const auto capped = document_leaf_diff(document, changed, 1);
    check(capped.size() == 1, "leaf diff honors the limit");
  }
  {
    // Type changes and array-length drift report whole nodes/leaves.
    const nlohmann::ordered_json a{{"list", {1, 2, 3}}, {"kind", "x"}};
    const nlohmann::ordered_json b{{"list", {1, 2}}, {"kind", 7}};
    const auto leaves = document_leaf_diff(a, b);
    check(leaves.size() == 2 && leaves[0].path == "list[2]" &&
              leaves[0].actual == "<absent>" &&
              leaves[1].path == "kind" && leaves[1].actual == "7",
          "array shrink and scalar type change localized");
  }

  if (failures != 0) {
    std::cerr << failures << " replay checks failed\n";
    return 1;
  }
  std::cout << "replay tests passed\n";
  return 0;
}
