// Coverage for the editor project document: round-trip fidelity and
// all-or-nothing rejection of malformed input. The document is the
// editor's annotation layer (names/notes/bookmarks); generated system
// properties never enter it.

#include "../app/editor_project.hpp"

#include <iostream>
#include <stdexcept>
#include <string>

using namespace stellar::editor;

namespace {

void require(bool condition, const char *message) {
  if (!condition) throw std::runtime_error(message);
}

void rejects(std::string_view text, const char *message) {
  try {
    (void)parse_project(text);
  } catch (const std::runtime_error &) {
    return;
  }
  throw std::runtime_error(message);
}

} // namespace

int main() {
  try {
    // Round-trip preserves every field, including escapes and unicode.
    EditorProject project;
    project.seed = 8374837;
    project.system_count = 500;
    project.edits[12].name = "Sol Prime \"North\"";
    project.edits[12].note = "survey target \\ alpha";
    project.edits[12].bookmarked = true;
    project.edits[77].name = "Kepler-\xc3\xa9toile"; // UTF-8: "Kepler-étoile"
    project.edits[300].note = "note only";
    project.edits[301].bookmarked = true;
    project.edits[302].anomaly = true; // trait override: YES
    project.edits[303].rare_resource = false; // trait override: NO
    project.edits[303].pre_warp_civilization = true;
    project.edits[400]; // Fully empty row must not serialize.
    project.body_edits[3].name = "Earth";
    project.body_edits[3].bookmarked = true;
    project.body_edits[3].radius_earth = 1.25; // numeric override
    project.body_edits[3].orbit_au = 1.524; // stellar orbit override
    project.body_edits[3].mass_earth = 0.83; // mass override
    project.body_edits[3].eccentricity = 0.21; // orbit eccentricity override
    project.body_edits[3].inclination_degrees = 97.5; // orbit inclination override
    project.body_edits[9].note = "moon survey";
    project.name = "Survey Run \"Kestrel\"";
    const auto text = serialize_project(project);
    const auto restored = parse_project(text);
    require(restored.name == project.name, "project name did not round-trip");
    require(restored.seed == project.seed, "seed did not round-trip");
    require(restored.system_count == project.system_count,
            "system count did not round-trip");
    require(restored.edits.size() == 6, "empty edit rows must be omitted");
    require(restored.edits.at(12).name == project.edits.at(12).name,
            "display name did not round-trip");
    require(restored.edits.at(12).note == project.edits.at(12).note,
            "note did not round-trip");
    require(restored.edits.at(12).bookmarked, "bookmark did not round-trip");
    require(restored.edits.at(77).name == project.edits.at(77).name,
            "unicode name did not round-trip");
    require(!restored.edits.contains(400), "empty edit row serialized");
    require(restored.edits.at(302).anomaly && *restored.edits.at(302).anomaly,
            "anomaly override did not round-trip");
    require(restored.edits.at(303).rare_resource &&
                !*restored.edits.at(303).rare_resource,
            "explicit-false rare-resource override did not round-trip");
    require(restored.edits.at(303).pre_warp_civilization &&
                *restored.edits.at(303).pre_warp_civilization,
            "pre-warp override did not round-trip");
    require(!restored.edits.at(12).anomaly &&
                !restored.edits.at(12).rare_resource &&
                !restored.edits.at(12).pre_warp_civilization,
            "unset trait overrides must stay unset (AUTO follows generated)");
    require(restored.body_edits.size() == 2, "body edits did not round-trip");
    require(restored.body_edits.at(3).name == "Earth" &&
                restored.body_edits.at(3).bookmarked,
            "body name/bookmark did not round-trip");
    require(restored.body_edits.at(9).note == "moon survey",
            "body note did not round-trip");
    require(restored.body_edits.at(3).radius_earth &&
                *restored.body_edits.at(3).radius_earth == 1.25,
            "radius override did not round-trip");
    require(!restored.body_edits.at(9).radius_earth,
            "unset radius override must stay unset (AUTO follows generated)");
    require(restored.body_edits.at(3).orbit_au &&
                *restored.body_edits.at(3).orbit_au == 1.524,
            "orbit override did not round-trip");
    require(!restored.body_edits.at(9).orbit_au,
            "unset orbit override must stay unset (AUTO follows generated)");
    require(restored.body_edits.at(3).mass_earth &&
                *restored.body_edits.at(3).mass_earth == 0.83,
            "mass override did not round-trip");
    require(!restored.body_edits.at(9).mass_earth,
            "unset mass override must stay unset (AUTO follows generated)");
    require(restored.body_edits.at(3).eccentricity &&
                *restored.body_edits.at(3).eccentricity == 0.21,
            "eccentricity override did not round-trip");
    require(!restored.body_edits.at(9).eccentricity,
            "unset eccentricity override must stay unset (AUTO follows generated)");

    // Eccentricity is the one override where zero is meaningful (circular)
    // and AnalyticOrbit rejects >= 0.95 — the codec enforces the same range.
    {
      const auto circular = parse_project(
          R"({"schemaVersion":1,"seed":5,"systems":250,"edits":[],"bodyEdits":[{"id":3,"name":"","note":"","bookmarked":false,"eccentricity":0.0}]})");
      require(circular.body_edits.at(3).eccentricity &&
                  *circular.body_edits.at(3).eccentricity == 0.0,
              "zero eccentricity must round-trip as a real override");
      const auto hyperbolic = parse_project(
          R"({"schemaVersion":1,"seed":5,"systems":250,"edits":[],"bodyEdits":[{"id":3,"name":"","note":"","bookmarked":false,"eccentricity":1.2}]})");
      require(!hyperbolic.body_edits.contains(3),
              "out-of-range eccentricity must not serialize an override");
    }
    require(restored.body_edits.at(3).inclination_degrees &&
                *restored.body_edits.at(3).inclination_degrees == 97.5,
            "inclination override did not round-trip");
    require(!restored.body_edits.at(9).inclination_degrees,
            "unset inclination override must stay unset (AUTO follows generated)");

    // Inclination is bounded to the generated 0-180 degree domain.
    {
      const auto retrograde = parse_project(
          R"({"schemaVersion":1,"seed":5,"systems":250,"edits":[],"bodyEdits":[{"id":3,"name":"","note":"","bookmarked":false,"inclinationDeg":140.0}]})");
      require(retrograde.body_edits.at(3).inclination_degrees &&
                  *retrograde.body_edits.at(3).inclination_degrees == 140.0,
              "retrograde inclination must round-trip as a real override");
      const auto out_of_domain = parse_project(
          R"({"schemaVersion":1,"seed":5,"systems":250,"edits":[],"bodyEdits":[{"id":3,"name":"","note":"","bookmarked":false,"inclinationDeg":190.0}]})");
      require(!out_of_domain.body_edits.contains(3),
              "out-of-domain inclination must not serialize an override");
    }

    // Documents without the additive bodyEdits array still parse.
    const auto legacy = parse_project(
        R"({"schemaVersion":1,"seed":5,"systems":250,"edits":[{"id":7,"name":"x","note":"","bookmarked":false}]})");
    require(legacy.edits.at(7).name == "x", "v1 document lost system edits");
    require(legacy.body_edits.empty(), "missing bodyEdits must parse empty");
    require(legacy.name.empty(), "missing name must parse empty");

    // Malformed and unsupported inputs reject wholesale.
    rejects("not json at all", "non-JSON input was accepted");
    rejects("{", "truncated JSON was accepted");
    rejects(R"({"schemaVersion":1,"seed":5,"systems":250})",
            "missing edits array was accepted");
    rejects(R"({"schemaVersion":2,"seed":5,"systems":250,"edits":[]})",
            "unsupported schema version was accepted");
    rejects(R"({"seed":5,"systems":250,"edits":[]})",
            "missing schemaVersion was accepted");
    rejects(R"({"schemaVersion":1,"systems":250,"edits":[]})",
            "missing seed was accepted");
    rejects(R"({"schemaVersion":1,"seed":5,"systems":250,"edits":[{"name":"x"}]})",
            "edit row without id was accepted");
    rejects(R"({"schemaVersion":1,"seed":5,"systems":250,"edits":{"a":1}})",
            "non-array edits was accepted");

    // A documented empty project is valid.
    const auto empty = parse_project(
        serialize_project(EditorProject{.seed = 1, .system_count = 250}));
    require(empty.edits.empty(), "empty project parsed with edits");

    // Project names slug into filesystem-safe Save-As filenames.
    require(sanitize_project_name("Survey Run \"Kestrel\"") ==
                "survey-run-kestrel",
            "project name did not slug correctly");
    require(sanitize_project_name("  ---  ") == "",
            "unusable name must produce an empty slug");
    require(sanitize_project_name("Alpha  Centauri   Prime!") ==
                "alpha-centauri-prime",
            "whitespace runs must collapse to single dashes");

    std::cout << "Editor project document checks passed.\n";
    return 0;
  } catch (const std::exception &error) {
    std::cerr << "Editor project checks failed: " << error.what() << '\n';
    return 1;
  }
}
