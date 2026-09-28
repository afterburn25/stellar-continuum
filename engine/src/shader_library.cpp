#include <stellar/engine/shader_library.hpp>

#include <algorithm>
#include <functional>
#include <sstream>
#include <utility>

namespace stellar::engine {
namespace {

std::uint64_t hash_key(std::string_view text) noexcept {
  std::uint64_t hash = 14695981039346656037ULL;
  for (const unsigned char c : text) {
    hash ^= c;
    hash *= 1099511628211ULL;
  }
  return hash;
}

} // namespace

ShaderFamilyId ShaderLibrary::add_family(ShaderFamily family) {
  const auto existing = family_by_name_.find(family.name);
  if (existing != family_by_name_.end())
    return existing->second;
  const auto id = static_cast<ShaderFamilyId>(families_.size());
  family_by_name_.emplace(family.name, id);
  families_.push_back(std::move(family));
  return id;
}

const ShaderFamily *ShaderLibrary::family(ShaderFamilyId id) const {
  return id < families_.size() ? &families_[id] : nullptr;
}

ShaderFamilyId ShaderLibrary::find_family(std::string_view name) const {
  const auto found = family_by_name_.find(std::string(name));
  return found == family_by_name_.end() ? ~ShaderFamilyId{0}
                                        : found->second;
}

ShaderVariantKey ShaderLibrary::variant_key(
    ShaderFamilyId family_id,
    const std::vector<std::pair<std::string, std::string>> &defines) const {
  auto sorted = defines;
  std::sort(sorted.begin(), sorted.end());
  std::ostringstream canonical;
  canonical << "f" << family_id << ";";
  for (const auto &[name, value] : sorted)
    canonical << name << "=" << value << ";";
  return hash_key(canonical.str());
}

const ShaderVariant *ShaderLibrary::variant(ShaderVariantKey key) const {
  const auto found = variants_.find(key);
  return found == variants_.end() ? nullptr : &found->second;
}

ShaderVariant &ShaderLibrary::get_or_create_variant(
    ShaderFamilyId family_id,
    const std::vector<std::pair<std::string, std::string>> &defines) {
  const auto key = variant_key(family_id, defines);
  auto [it, inserted] = variants_.try_emplace(key);
  if (inserted) {
    auto sorted = defines;
    std::sort(sorted.begin(), sorted.end());
    std::ostringstream canonical;
    for (const auto &[name, value] : sorted)
      canonical << name << "=" << value << ";";
    it->second = ShaderVariant{key, family_id, canonical.str(), false, 0, {}};
  }
  return it->second;
}

void ShaderLibrary::mark_compiled(ShaderVariantKey key,
                                  std::uint64_t artifact_hash,
                                  std::string diagnostics) {
  const auto found = variants_.find(key);
  if (found == variants_.end())
    return;
  found->second.compiled = true;
  found->second.artifact_hash = artifact_hash;
  found->second.diagnostics = std::move(diagnostics);
}

void ShaderLibrary::bump_family_version(ShaderFamilyId id) {
  if (id >= families_.size())
    return;
  ++families_[id].version;
  for (auto it = variants_.begin(); it != variants_.end();)
    if (it->second.family == id)
      it = variants_.erase(it);
    else
      ++it;
}

void ShaderLibrary::invalidate_all() {
  variants_.clear();
  for (auto &family : families_)
    ++family.version;
}

std::size_t ShaderLibrary::compiled_variant_count() const {
  std::size_t count = 0;
  for (const auto &[key, variant] : variants_)
    if (variant.compiled)
      ++count;
  return count;
}

} // namespace stellar::engine
