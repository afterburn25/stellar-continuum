#pragma once

#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace stellar::engine {

// Semantic version parsed from "major.minor.patch" (missing components
// default to 0). Comparison ignores any pre-release suffix.
struct PackageVersion {
  std::uint32_t major{}, minor{}, patch{};

  static std::optional<PackageVersion> parse(std::string_view text);
  std::string to_string() const;
  auto operator<=>(const PackageVersion &) const = default;
};

// A version constraint is a comma-separated list of comparators, e.g.
// ">=1.2.0", "==2.0.0", ">=1.0,<2.0". A bare version means "==".
bool version_satisfies(const PackageVersion &version,
                       std::string_view constraint);

struct PackageDependency {
  std::string id;
  std::string constraint; // empty = any version
};

// Declared in a package's manifest.json. Stable `id` is the content namespace
// (e.g. "stellar.base", "mod.aurora"); `priority` breaks load-order ties and
// decides which package wins a content-id conflict (higher wins).
struct PackageManifest {
  std::string id;
  std::string name;
  PackageVersion version{};
  int priority{};
  std::vector<PackageDependency> dependencies;
  // Namespaces whose content ids this package owns/overrides.
  std::vector<std::string> provides;
  std::string root_path;

  static std::optional<PackageManifest>
  parse(std::string_view json_document, std::string *error = nullptr);
};

struct PackageConflict {
  std::string namespace_id;
  std::string winner_id;
  std::string loser_id;
};

struct PackageLoadPlan {
  bool ok{true};
  std::vector<std::string> errors;
  // Resolved load order, dependencies first, ties broken by priority then id.
  std::vector<PackageManifest> order;
  std::vector<PackageConflict> conflicts;
};

// Registry of discovered content packages. The base game registers itself as
// a protected package; attempts to register a second package under a
// protected id are rejected rather than silently overriding.
class PackageRegistry {
public:
  void protect_namespace(std::string id);
  bool is_protected(std::string_view id) const;

  // Returns false when the id is already registered or is protected.
  bool add(PackageManifest manifest, std::string *error = nullptr);
  const PackageManifest *find(std::string_view id) const;
  std::vector<const PackageManifest *> manifests() const;

  // Topological load-order resolution. Fails (ok=false) on missing
  // dependencies or version-constraint violations; dependency cycles are
  // reported as errors.
  PackageLoadPlan resolve() const;

private:
  std::vector<PackageManifest> manifests_;
  std::vector<std::string> protected_ids_;
};

// Scans a directory for immediate child folders containing "package.json"
// and registers each valid manifest. Returns the number of packages added.
std::size_t scan_packages(PackageRegistry &registry,
                          const std::string &directory,
                          std::vector<std::string> *errors = nullptr);

// Result of verifying a world save's recorded package manifest against the
// currently resolved load plan. `extra_packages` is informational — content
// added since the save is not a compatibility failure. Saves predating the
// manifest format verify as manifest_present=false (unknown, not broken).
struct SavePackageReport {
  bool manifest_present{};
  std::vector<std::string> missing_packages;   // recorded, not loaded now
  std::vector<std::string> extra_packages;     // loaded now, not recorded
  // "id: recorded X.Y.Z != current A.B.C"
  std::vector<std::string> version_mismatches;
  bool compatible() const {
    return missing_packages.empty() && version_mismatches.empty();
  }
};

// Writes "<save>.packages.json" recording the resolved load plan — the
// content set a world save was produced under. A torn write degrades to a
// manifest that fails to parse, i.e. unknown rather than incompatible.
void write_save_package_manifest(const std::filesystem::path &save_path,
                                 const PackageLoadPlan &plan);

// Reads the recorded manifest (absent or corrupt -> manifest_present=false)
// and diffs it against the current plan. Report-only: callers decide whether
// an incompatible save may still load.
SavePackageReport
verify_save_package_manifest(const std::filesystem::path &save_path,
                             const PackageLoadPlan &plan);

} // namespace stellar::engine
