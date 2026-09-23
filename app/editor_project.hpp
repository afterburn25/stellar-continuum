// Editor project document: the authoring layer (display-name overrides,
// notes, bookmarks) persisted separately from authoritative generated
// system properties. Generated content always comes back from
// generate_stellar_catalog; this file only carries editor annotations.

#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>

namespace stellar::editor {

struct SystemEdit {
  std::string name, note;
  bool bookmarked{};
  // Trait overrides: unset follows the generated record; a set value wins.
  // They re-apply deterministically after regeneration because they live in
  // the annotation layer, not the generated catalog.
  std::optional<bool> anomaly, rare_resource, pre_warp_civilization;
  // Numeric property override (body edits only): unset follows the
  // generated radius_earth; a set value wins everywhere the editor reads it.
  std::optional<double> radius_earth;
  // Stellar orbit radius override in AU (star-orbiting bodies only): the
  // generated orbit's other elements stay; the ring and position recompute
  // from the patched AnalyticOrbit.
  std::optional<double> orbit_au;
  // Numeric property override (body edits only): unset follows the
  // generated mass_earth; a set value wins everywhere the editor reads it.
  // Surface gravity derives as effective_mass / effective_radius^2, the
  // same relationship generation applies — an overridden radius or mass
  // never leaves a stale gravity reading.
  std::optional<double> mass_earth;
};

struct EditorProject {
  std::int64_t seed{};
  int system_count{};
  // Keyed by stable catalog ids: system edits by StellarSystem::id, body
  // edits by PlanetaryBody::id. Generated properties never enter here.
  std::unordered_map<int, SystemEdit> edits;
  std::unordered_map<int, SystemEdit> body_edits;
  // Free-form document name shown in the toolbar and used by Save-As /
  // recent-projects workflows. Last so aggregate inits stay stable.
  std::string name;
};

// JSON document with schemaVersion. Rows with no annotation are omitted.
std::string serialize_project(const EditorProject &project);

// Throws std::runtime_error on malformed JSON, missing/unsupported
// schemaVersion, or a structurally invalid edits array.
EditorProject parse_project(std::string_view text);

// Lowercases a project name into a filesystem-safe slug: alnum runs stay,
// anything else collapses to single dashes, edges are trimmed. Returns ""
// when nothing usable remains (caller falls back to a default filename).
std::string sanitize_project_name(std::string_view name);

} // namespace stellar::editor
