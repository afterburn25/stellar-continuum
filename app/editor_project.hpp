// Editor project document: the authoring layer (display-name overrides,
// notes, bookmarks) persisted separately from authoritative generated
// system properties. Generated content always comes back from
// generate_stellar_catalog; this file only carries editor annotations.

#pragma once

#include <cstdint>
#include <string>
#include <string_view>
#include <unordered_map>

namespace stellar::editor {

struct SystemEdit {
  std::string name, note;
  bool bookmarked{};
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

} // namespace stellar::editor
