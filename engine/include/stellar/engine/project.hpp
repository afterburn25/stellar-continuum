#pragma once

#include <filesystem>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace stellar::engine {

// A game project rooted at a directory containing "project.stellar.json".
// The manifest declares the project's content roots — directories scanned
// for package.json manifests (see package.hpp) — so engine tools can load
// any game's content without linking its module. `id` is the project's
// content namespace (e.g. "game.aurora"); content packages it owns declare
// the same namespace via `provides`.
struct EngineProject {
  std::string name;
  std::string id;
  // Recorded for forward compatibility; loaded but not enforced.
  std::string engine_version;
  // Content roots relative to `root`; each is scanned for packages.
  std::vector<std::string> content_dirs;
  std::filesystem::path root;

  static constexpr std::string_view manifest_filename{"project.stellar.json"};

  // All-or-nothing manifest parse: returns nullopt and reports `error` on
  // any malformed or missing field, leaving no partial state.
  static std::optional<EngineProject> load(const std::filesystem::path &root,
                                           std::string *error = nullptr);
  std::string to_json() const;
  // Atomically writes the manifest back to <root>/project.stellar.json
  // (write_file_atomically: temp file, flush, replace). Throws on failure.
  void save() const;
};

// "My Game" -> "game.my-game" style namespace slug. Empty or unparsable
// names fall back to "game.project".
std::string sanitize_project_id(std::string_view name);

// Scaffolds a minimal project at `root`: the manifest, a base content
// package under packages/<id>/ (owned namespace = the project id), an empty
// content directory, and a starter host source file. Refuses to overwrite
// an existing manifest. Returns false and reports `error` on failure.
bool create_project(const std::filesystem::path &root, std::string_view name,
                    std::string_view engine_version, std::string *error);

// Immediate child directories of `directory` containing a manifest file
// (the manifest itself is not parsed here — EngineProject::load validates).
std::vector<std::filesystem::path>
find_projects(const std::filesystem::path &directory);

} // namespace stellar::engine
