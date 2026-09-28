#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace stellar::engine {

// Shader family/variant management. Families are named shader programs with
// declared permutation axes (e.g. "planet" with defines ATMOSPHERE, RINGS).
// Variants are canonicalized keys (sorted define list) so "A=1,B=0" and
// "B=0,A=1" hit the same cache entry. Compilation itself is the backend's
// job; this layer owns identity, versioning, and the cache manifest.

using ShaderFamilyId = std::uint32_t;
using ShaderVariantKey = std::uint64_t;

struct ShaderFamily {
  std::string name;             // e.g. "planet", "ring", "vfx_sprite"
  std::string source_path;      // canonical source for diagnostics
  std::uint32_t version{1};     // bump to invalidate all variants
};

struct ShaderVariant {
  ShaderVariantKey key{};
  ShaderFamilyId family{};
  // Canonical "NAME=VALUE;NAME=VALUE" form, sorted.
  std::string defines;
  bool compiled{};
  std::uint64_t artifact_hash{};
  std::string diagnostics;      // last compile error/warning, if any
};

class ShaderLibrary {
public:
  ShaderFamilyId add_family(ShaderFamily family);
  const ShaderFamily *family(ShaderFamilyId id) const;
  ShaderFamilyId find_family(std::string_view name) const;

  // Canonicalizes defines and returns the stable variant key.
  ShaderVariantKey variant_key(ShaderFamilyId family,
                               const std::vector<std::pair<std::string,
                                                           std::string>>
                                   &defines) const;
  const ShaderVariant *variant(ShaderVariantKey key) const;
  ShaderVariant &get_or_create_variant(
      ShaderFamilyId family,
      const std::vector<std::pair<std::string, std::string>> &defines);

  // Marks a variant compiled with its artifact hash / diagnostics.
  void mark_compiled(ShaderVariantKey key, std::uint64_t artifact_hash,
                     std::string diagnostics = {});

  // Invalidates every variant of a family (source changed/version bump).
  void bump_family_version(ShaderFamilyId id);
  void invalidate_all();

  std::size_t family_count() const noexcept { return families_.size(); }
  std::size_t variant_count() const noexcept { return variants_.size(); }
  std::size_t compiled_variant_count() const;

private:
  std::vector<ShaderFamily> families_;
  std::unordered_map<std::string, ShaderFamilyId> family_by_name_;
  std::unordered_map<ShaderVariantKey, ShaderVariant> variants_;
};

} // namespace stellar::engine
