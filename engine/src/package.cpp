#include <stellar/engine/package.hpp>

#include <nlohmann/json.hpp>

#include <algorithm>
#include <charconv>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <unordered_map>
#include <unordered_set>

namespace stellar::engine {
namespace {

std::optional<std::uint32_t> parse_component(std::string_view text) {
  std::uint32_t value{};
  const auto *begin = text.data();
  const auto *end = begin + text.size();
  const auto result = std::from_chars(begin, end, value);
  if (result.ec != std::errc{} || result.ptr != end)
    return std::nullopt;
  return value;
}

struct Comparator {
  char op; // '<', '>', '=', '!', 'l' (<=), 'g' (>=)
  PackageVersion version;
};

std::optional<Comparator> parse_comparator(std::string_view text) {
  text = text.substr(text.find_first_not_of(" \t"));
  if (text.empty())
    return std::nullopt;
  char op = '=';
  if (text.starts_with(">=")) {
    op = 'g';
    text.remove_prefix(2);
  } else if (text.starts_with("<=")) {
    op = 'l';
    text.remove_prefix(2);
  } else if (text.starts_with("==")) {
    op = '=';
    text.remove_prefix(2);
  } else if (text.starts_with("!=")) {
    op = '!';
    text.remove_prefix(2);
  } else if (text.front() == '>' || text.front() == '<') {
    op = text.front();
    text.remove_prefix(1);
  }
  const auto version = PackageVersion::parse(text);
  if (!version)
    return std::nullopt;
  return Comparator{op, *version};
}

} // namespace

std::optional<PackageVersion> PackageVersion::parse(std::string_view text) {
  // Strip any pre-release/build suffix.
  if (const auto dash = text.find('-'); dash != std::string_view::npos)
    text = text.substr(0, dash);
  if (const auto plus = text.find('+'); plus != std::string_view::npos)
    text = text.substr(0, plus);
  PackageVersion result;
  std::size_t start = 0;
  std::uint32_t *fields[]{&result.major, &result.minor, &result.patch};
  std::size_t field = 0;
  while (field < 3 && start < text.size()) {
    const auto dot = text.find('.', start);
    const auto part = text.substr(start, dot == std::string_view::npos
                                           ? std::string_view::npos
                                           : dot - start);
    if (part.empty())
      return std::nullopt;
    const auto value = parse_component(part);
    if (!value)
      return std::nullopt;
    *fields[field++] = *value;
    if (dot == std::string_view::npos) {
      start = text.size();
      break;
    }
    start = dot + 1;
  }
  if (start < text.size())
    return std::nullopt; // more than three components
  return result;
}

std::string PackageVersion::to_string() const {
  return std::to_string(major) + "." + std::to_string(minor) + "." +
         std::to_string(patch);
}

bool version_satisfies(const PackageVersion &version,
                       std::string_view constraint) {
  if (constraint.empty())
    return true;
  std::size_t start = 0;
  while (start <= constraint.size()) {
    const auto comma = constraint.find(',', start);
    const auto part = constraint.substr(
        start, comma == std::string_view::npos ? std::string_view::npos
                                               : comma - start);
    if (!part.empty()) {
      const auto comparator = parse_comparator(part);
      if (!comparator)
        return false;
      const auto cmp = version <=> comparator->version;
      bool satisfied = false;
      switch (comparator->op) {
      case '=': satisfied = cmp == 0; break;
      case '!': satisfied = cmp != 0; break;
      case '<': satisfied = cmp < 0; break;
      case '>': satisfied = cmp > 0; break;
      case 'l': satisfied = cmp <= 0; break;
      case 'g': satisfied = cmp >= 0; break;
      }
      if (!satisfied)
        return false;
    }
    if (comma == std::string_view::npos)
      break;
    start = comma + 1;
  }
  return true;
}

std::optional<PackageManifest>
PackageManifest::parse(std::string_view json_document, std::string *error) {
  nlohmann::json doc;
  try {
    doc = nlohmann::json::parse(json_document);
  } catch (const std::exception &ex) {
    if (error != nullptr)
      *error = ex.what();
    return std::nullopt;
  }
  auto fail = [&](const std::string &message) {
    if (error != nullptr)
      *error = message;
    return std::nullopt;
  };
  if (!doc.is_object())
    return fail("manifest must be a JSON object");
  PackageManifest manifest;
  manifest.id = doc.value("id", std::string{});
  manifest.name = doc.value("name", manifest.id);
  if (manifest.id.empty())
    return fail("manifest requires a non-empty 'id'");
  const auto version_text = doc.value("version", std::string{"1.0.0"});
  const auto version = PackageVersion::parse(version_text);
  if (!version)
    return fail("manifest version '" + version_text + "' is not semver");
  manifest.version = *version;
  manifest.priority = doc.value("priority", 0);
  if (doc.contains("dependencies")) {
    if (!doc.at("dependencies").is_array())
      return fail("'dependencies' must be an array");
    for (const auto &entry : doc.at("dependencies")) {
      PackageDependency dependency;
      if (entry.is_string())
        dependency.id = entry.get<std::string>();
      else if (entry.is_object()) {
        dependency.id = entry.value("id", std::string{});
        dependency.constraint = entry.value("version", std::string{});
      } else
        return fail("dependency entries must be strings or objects");
      if (dependency.id.empty())
        return fail("dependency requires an 'id'");
      manifest.dependencies.push_back(std::move(dependency));
    }
  }
  if (doc.contains("provides")) {
    if (!doc.at("provides").is_array())
      return fail("'provides' must be an array");
    for (const auto &entry : doc.at("provides")) {
      if (!entry.is_string())
        return fail("'provides' entries must be strings");
      manifest.provides.push_back(entry.get<std::string>());
    }
  }
  manifest.root_path = doc.value("root", std::string{});
  return manifest;
}

void PackageRegistry::protect_namespace(std::string id) {
  if (!is_protected(id))
    protected_ids_.push_back(std::move(id));
}
bool PackageRegistry::is_protected(std::string_view id) const {
  return std::find(protected_ids_.begin(), protected_ids_.end(), id) !=
         protected_ids_.end();
}

bool PackageRegistry::add(PackageManifest manifest, std::string *error) {
  if (find(manifest.id) != nullptr) {
    if (error != nullptr)
      *error = "package id '" + manifest.id + "' is already registered";
    return false;
  }
  if (is_protected(manifest.id)) {
    if (error != nullptr)
      *error = "package id '" + manifest.id + "' is a protected namespace";
    return false;
  }
  manifests_.push_back(std::move(manifest));
  return true;
}

const PackageManifest *PackageRegistry::find(std::string_view id) const {
  for (const auto &manifest : manifests_)
    if (manifest.id == id)
      return &manifest;
  return nullptr;
}

std::vector<const PackageManifest *> PackageRegistry::manifests() const {
  std::vector<const PackageManifest *> result;
  result.reserve(manifests_.size());
  for (const auto &manifest : manifests_)
    result.push_back(&manifest);
  return result;
}

PackageLoadPlan PackageRegistry::resolve() const {
  PackageLoadPlan plan;

  // Dependency validation.
  std::unordered_map<std::string, const PackageManifest *> by_id;
  for (const auto &manifest : manifests_)
    by_id.emplace(manifest.id, &manifest);
  for (const auto &manifest : manifests_) {
    for (const auto &dependency : manifest.dependencies) {
      const auto found = by_id.find(dependency.id);
      if (found == by_id.end()) {
        plan.ok = false;
        plan.errors.push_back("package '" + manifest.id +
                              "' requires missing package '" + dependency.id +
                              "'");
        continue;
      }
      if (!version_satisfies(found->second->version, dependency.constraint)) {
        plan.ok = false;
        plan.errors.push_back("package '" + manifest.id + "' requires '" +
                              dependency.id + "' " + dependency.constraint +
                              " but found " +
                              found->second->version.to_string());
      }
    }
  }

  // Deterministic topological order: Kahn's algorithm, always picking the
  // lowest-priority... no — the *ready* package with smallest
  // (priority, id) goes first so ties are stable.
  std::unordered_map<std::string, std::size_t> indegree;
  std::unordered_map<std::string, std::vector<std::string>> dependents;
  for (const auto &manifest : manifests_) {
    indegree.try_emplace(manifest.id, 0);
    for (const auto &dependency : manifest.dependencies)
      if (by_id.contains(dependency.id)) {
        ++indegree[manifest.id];
        dependents[dependency.id].push_back(manifest.id);
      }
  }
  auto rank = [&](const std::string &id) {
    const auto *manifest = by_id.at(id);
    return std::pair{-manifest->priority, manifest->id};
  };
  std::vector<std::string> ready;
  for (const auto &[id, degree] : indegree)
    if (degree == 0)
      ready.push_back(id);
  std::unordered_set<std::string> emitted;
  while (!ready.empty()) {
    std::sort(ready.begin(), ready.end(),
              [&](const auto &a, const auto &b) { return rank(a) < rank(b); });
    const auto id = ready.front();
    ready.erase(ready.begin());
    if (!emitted.insert(id).second)
      continue;
    plan.order.push_back(*by_id.at(id));
    for (const auto &dependent : dependents[id])
      if (--indegree[dependent] == 0)
        ready.push_back(dependent);
  }
  if (plan.order.size() < manifests_.size()) {
    plan.ok = false;
    for (const auto &manifest : manifests_)
      if (!emitted.contains(manifest.id))
        plan.errors.push_back("package '" + manifest.id +
                              "' is part of a dependency cycle");
  }

  // Content-namespace conflicts: higher priority wins; equal priority is a
  // genuine conflict reported with the lexicographically smaller id winning
  // for determinism.
  std::unordered_map<std::string, std::string> owner;
  for (const auto &manifest : plan.order)
    for (const auto &ns : manifest.provides) {
      const auto found = owner.find(ns);
      if (found == owner.end()) {
        owner.emplace(ns, manifest.id);
        continue;
      }
      const auto *incumbent = by_id.at(found->second);
      const bool manifest_wins =
          manifest.priority > incumbent->priority ||
          (manifest.priority == incumbent->priority &&
           manifest.id < incumbent->id);
      plan.conflicts.push_back(PackageConflict{
          ns, manifest_wins ? manifest.id : incumbent->id,
          manifest_wins ? incumbent->id : manifest.id});
      if (manifest_wins)
        found->second = manifest.id;
    }
  return plan;
}

std::size_t scan_packages(PackageRegistry &registry,
                          const std::string &directory,
                          std::vector<std::string> *errors) {
  namespace fs = std::filesystem;
  std::size_t added = 0;
  std::error_code ec;
  if (!fs::is_directory(directory, ec))
    return 0;
  for (const auto &entry : fs::directory_iterator(directory, ec)) {
    if (!entry.is_directory())
      continue;
    const auto manifest_path = entry.path() / "package.json";
    if (!fs::exists(manifest_path, ec))
      continue;
    std::ifstream stream(manifest_path, std::ios::binary);
    std::ostringstream contents;
    contents << stream.rdbuf();
    std::string error;
    auto manifest = PackageManifest::parse(contents.str(), &error);
    if (!manifest) {
      if (errors != nullptr)
        errors->push_back(manifest_path.string() + ": " + error);
      continue;
    }
    if (manifest->root_path.empty())
      manifest->root_path = entry.path().string();
    if (!registry.add(std::move(*manifest), &error)) {
      if (errors != nullptr)
        errors->push_back(manifest_path.string() + ": " + error);
      continue;
    }
    ++added;
  }
  return added;
}

namespace {
std::filesystem::path save_manifest_path(const std::filesystem::path &save_path) {
  return save_path.parent_path() /
         (save_path.filename().string() + ".packages.json");
}
} // namespace

void write_save_package_manifest(const std::filesystem::path &save_path,
                                 const PackageLoadPlan &plan) {
  nlohmann::json packages = nlohmann::json::array();
  for (const auto &m : plan.order)
    packages.push_back({{"id", m.id}, {"version", m.version.to_string()}});
  const nlohmann::json doc{{"schemaVersion", 1}, {"packages", packages}};
  std::ofstream out(save_manifest_path(save_path));
  if (out)
    out << doc.dump(2);
}

SavePackageReport
verify_save_package_manifest(const std::filesystem::path &save_path,
                             const PackageLoadPlan &plan) {
  SavePackageReport report;
  std::ifstream in(save_manifest_path(save_path), std::ios::binary);
  if (!in)
    return report;
  nlohmann::json doc;
  try {
    doc = nlohmann::json::parse(in);
  } catch (...) {
    return report;
  }
  const auto it_packages = doc.find("packages");
  if (!doc.is_object() || it_packages == doc.end() ||
      !it_packages->is_array())
    return report;
  report.manifest_present = true;
  std::unordered_map<std::string, std::string> recorded;
  for (const auto &p : *it_packages) {
    if (!p.is_object())
      continue;
    const std::string id = p.value("id", "");
    if (id.empty())
      continue;
    recorded[id] = p.value("version", "");
  }
  std::unordered_set<std::string> current_ids;
  for (const auto &m : plan.order) {
    current_ids.insert(m.id);
    const auto it = recorded.find(m.id);
    if (it == recorded.end()) {
      report.extra_packages.push_back(m.id);
      continue;
    }
    const std::string current = m.version.to_string();
    if (it->second != current)
      report.version_mismatches.push_back(m.id + ": recorded " + it->second +
                                          " != current " + current);
  }
  for (const auto &[id, version] : recorded)
    if (!current_ids.contains(id))
      report.missing_packages.push_back(id);
  std::sort(report.missing_packages.begin(), report.missing_packages.end());
  std::sort(report.extra_packages.begin(), report.extra_packages.end());
  std::sort(report.version_mismatches.begin(), report.version_mismatches.end());
  return report;
}

} // namespace stellar::engine
