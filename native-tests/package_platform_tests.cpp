#include <stellar/engine/package.hpp>
#include <stellar/engine/platform_services.hpp>

#include <filesystem>
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

class FakeBackend final : public PlatformServicesBackend {
public:
  std::string name() const override { return "fake"; }
  bool available() const override { return true; }
  PlatformUser user() const override {
    return {true, 42, "Commander Test"};
  }
  bool supports(PlatformFeature feature) const override {
    return feature == PlatformFeature::Achievements ||
           feature == PlatformFeature::RichPresence;
  }
  FeatureResult unlock_achievement(const std::string &) override {
    ++achievements;
    return {true, "unlocked"};
  }
  FeatureResult set_rich_presence(const std::string &,
                                  const std::string &) override {
    ++presence;
    return {true, "set"};
  }
  FeatureResult request_cloud_sync() override {
    return {false, "cloud unsupported"};
  }
  int achievements{}, presence{};
};
} // namespace

int main() {
  // --- PackageVersion ---
  const auto v = PackageVersion::parse("1.2.3");
  check(v && v->major == 1 && v->minor == 2 && v->patch == 3,
        "semver parse");
  check(!PackageVersion::parse("1.2.x"), "rejects non-numeric");
  check(!PackageVersion::parse("1.2.3.4"), "rejects four components");
  check(PackageVersion::parse("2.0") && PackageVersion::parse("2.0")->patch == 0,
        "two-component version ok");
  check(version_satisfies(*v, ">=1.0.0"), ">= constraint");
  check(!version_satisfies(*v, ">1.2.3"), "> strict");
  check(version_satisfies(*v, ">=1.0,<2.0"), "range constraint");
  check(!version_satisfies(*v, "==1.2.4"), "== mismatch");
  check(version_satisfies(*v, "1.2.3"), "bare version means ==");
  check(!version_satisfies(*v, "!=1.2.3"), "!= exclusion");

  // --- Manifest parse ---
  std::string error;
  const auto manifest = PackageManifest::parse(R"({
    "id": "mod.aurora",
    "name": "Aurora Pack",
    "version": "1.4.0",
    "priority": 10,
    "dependencies": [{"id": "stellar.base", "version": ">=1.0"}],
    "provides": ["planet_textures"]
  })",
                                               &error);
  check(manifest.has_value(), error.c_str());
  check(manifest->dependencies.size() == 1 &&
            manifest->dependencies[0].id == "stellar.base",
        "dependency parsed");
  check(!PackageManifest::parse(R"({"version":"1.0"})", &error),
        "manifest without id rejected");

  // --- Registry + resolve ---
  PackageRegistry registry;
  registry.protect_namespace("stellar.base");
  PackageManifest blocked;
  blocked.id = "stellar.base";
  check(!registry.add(blocked, &error), "protected namespace rejected");

  PackageManifest base;
  base.id = "stellar.base";
  base.version = *PackageVersion::parse("1.0.0");
  // On a registry without protection, first-party ids register normally.
  {
    PackageRegistry second;
    check(second.add(base, &error), "base registers on fresh registry");
  }
  PackageRegistry mods;
  check(mods.add(base, &error), "base package registers");
  check(mods.add(*manifest, &error), "mod registers");

  PackageManifest missing_dep;
  missing_dep.id = "mod.broken";
  missing_dep.dependencies.push_back({"mod.absent", ">=1.0"});
  check(mods.add(missing_dep, &error), "broken mod registers");

  const auto plan = mods.resolve();
  check(!plan.ok, "missing dependency fails the plan");
  check(plan.order.size() == 3,
        "topological order still covers all packages");

  PackageRegistry clean;
  clean.add(base, nullptr);
  clean.add(*manifest, nullptr);
  const auto good = clean.resolve();
  check(good.ok, "clean registry resolves");
  check(good.order.size() == 2 && good.order[0].id == "stellar.base" &&
            good.order[1].id == "mod.aurora",
        "dependency order: base before dependent");

  // Conflict: two packages providing the same namespace.
  PackageRegistry conflicted;
  PackageManifest a, b;
  a.id = "mod.a";
  a.provides = {"planet_textures"};
  a.priority = 5;
  b.id = "mod.b";
  b.provides = {"planet_textures"};
  b.priority = 9;
  conflicted.add(a, nullptr);
  conflicted.add(b, nullptr);
  const auto with_conflict = conflicted.resolve();
  check(with_conflict.ok, "conflicts don't fail resolution");
  check(with_conflict.conflicts.size() == 1 &&
            with_conflict.conflicts[0].winner_id == "mod.b",
        "higher priority wins the conflict");

  // --- Save package-manifest attestation ---
  const auto save_path = std::filesystem::temp_directory_path() /
                         "stellar_save_manifest_test.stw";
  const auto manifest_file = save_path.parent_path() /
                             (save_path.filename().string() + ".packages.json");

  // No manifest: pre-manifest saves verify as unknown, not incompatible.
  std::filesystem::remove(manifest_file);
  const auto absent = verify_save_package_manifest(save_path, good);
  check(!absent.manifest_present && absent.compatible(),
        "absent manifest attests as unknown/compatible");

  write_save_package_manifest(save_path, good);
  const auto same = verify_save_package_manifest(save_path, good);
  check(same.manifest_present && same.compatible() &&
            same.extra_packages.empty(),
        "same plan attests clean");

  // A removed mod reports missing; a version bump reports the mismatch.
  PackageRegistry reduced;
  reduced.add(base, nullptr);
  const auto reduced_plan = reduced.resolve();
  const auto missing = verify_save_package_manifest(save_path, reduced_plan);
  check(missing.missing_packages.size() == 1 &&
            missing.missing_packages[0] == "mod.aurora",
        "removed package reports missing");
  check(!missing.compatible(), "missing package is incompatible");

  PackageManifest bumped = base;
  bumped.version = *PackageVersion::parse("1.1.0");
  PackageRegistry bumped_registry;
  bumped_registry.add(bumped, nullptr);
  bumped_registry.add(*manifest, nullptr);
  const auto bumped_plan = bumped_registry.resolve();
  const auto mismatched = verify_save_package_manifest(save_path, bumped_plan);
  check(mismatched.version_mismatches.size() == 1 &&
            mismatched.version_mismatches[0].find("stellar.base") !=
                std::string::npos,
        "version bump reports mismatch");

  // Content added since the save is informational, not a failure.
  PackageManifest extra;
  extra.id = "mod.extra";
  PackageRegistry grown;
  grown.add(base, nullptr);
  grown.add(*manifest, nullptr);
  grown.add(extra, nullptr);
  const auto grown_plan = grown.resolve();
  const auto added = verify_save_package_manifest(save_path, grown_plan);
  check(added.extra_packages.size() == 1 &&
            added.extra_packages[0] == "mod.extra" && added.compatible(),
        "new package is informational");
  std::filesystem::remove(manifest_file);

  // --- Platform services ---
  PlatformServices services;
  auto status = services.status();
  check(!status.available && status.backend_name == "null",
        "standalone starts on null backend");
  check(!services.supports(PlatformFeature::Achievements),
        "null backend supports nothing");
  check(!services.unlock_achievement("X").ok,
        "achievement call safely no-ops");

  auto fake = std::make_unique<FakeBackend>();
  auto *fake_ptr = fake.get();
  services.attach_backend(std::move(fake));
  status = services.status();
  check(status.available && status.user.id == 42 &&
            status.user.display_name == "Commander Test",
        "attached backend reports user");
  check(services.supports(PlatformFeature::Achievements),
        "backend feature advertised");
  check(services.unlock_achievement("FIRST_CONTACT").ok &&
            fake_ptr->achievements == 1,
        "achievement reaches backend");
  check(services.set_rich_presence("status", "exploring").ok &&
            fake_ptr->presence == 1,
        "rich presence reaches backend");
  check(!services.request_cloud_sync().ok,
        "unsupported feature propagates failure");

  services.detach_backend();
  check(!services.status().available, "detach returns to null backend");

  if (failures == 0)
    std::cout << "Package and platform services tests passed\n";
  return failures == 0 ? 0 : 1;
}
