#include <stellar/core/galaxy_generation_metadata.hpp>
#include <stellar/core/stellar_population_profiles.hpp>

#include <cmath>
#include <cstdint>
#include <stdexcept>
#include <string_view>

namespace stellar::core {
namespace {
bool utf8_whitespace(std::string_view &value) {
  const auto first = static_cast<unsigned char>(value.front());
  std::uint32_t code_point = first;
  std::size_t length = 1;
  if ((first & 0xe0) == 0xc0) { code_point = first & 0x1f; length = 2; }
  else if ((first & 0xf0) == 0xe0) { code_point = first & 0x0f; length = 3; }
  else if ((first & 0xf8) == 0xf0) { code_point = first & 0x07; length = 4; }
  else if (first >= 0x80) return false;
  if (value.size() < length) return false;
  for (std::size_t index = 1; index < length; ++index) {
    const auto continuation = static_cast<unsigned char>(value[index]);
    if ((continuation & 0xc0) != 0x80) return false;
    code_point = (code_point << 6) | (continuation & 0x3f);
  }
  value.remove_prefix(length);
  return (code_point >= 0x09 && code_point <= 0x0d) || code_point == 0x20 ||
         code_point == 0x85 || code_point == 0xa0 || code_point == 0x1680 ||
         (code_point >= 0x2000 && code_point <= 0x200a) ||
         code_point == 0x2028 || code_point == 0x2029 || code_point == 0x202f ||
         code_point == 0x205f || code_point == 0x3000;
}
bool blank(std::string_view value) {
  if (value.empty()) return true;
  while (!value.empty()) if (!utf8_whitespace(value)) return false;
  return true;
}
bool dotnet_float_equal(float left, float right) {
  return left == right || (std::isnan(left) && std::isnan(right));
}
} // namespace

bool GalacticCoreMetadata::operator==(const GalacticCoreMetadata &other) const {
  return landmark_key == other.landmark_key && dotnet_float_equal(x, other.x) &&
         dotnet_float_equal(y, other.y) &&
         dotnet_float_equal(exclusion_radius, other.exclusion_radius) && black_hole == other.black_hole;
}

std::optional<GalacticCoreMetadata>
validate_galactic_core_metadata(
    const std::optional<GalacticCoreMetadata> &core,
    std::span<const StellarSystem> systems) {
  if (!core)
    return std::nullopt;
  if (core->landmark_key != GalacticCoreMetadata::stable_landmark_key ||
      !std::isfinite(core->x) || !std::isfinite(core->y) ||
      !std::isfinite(core->exclusion_radius) || core->exclusion_radius <= 0.0F)
    throw std::runtime_error("Campaign galactic-core metadata is invalid.");

  if(core->black_hole) validate_central_black_hole(*core->black_hole);
  const auto radius_squared = core->exclusion_radius * core->exclusion_radius;
  for (const auto &system : systems) {
    const auto dx = system.position.x - core->x;
    const auto dy = system.position.y - core->y;
    if (dx * dx + dy * dy < radius_squared)
      throw std::runtime_error(
          "Campaign galactic-core metadata overlaps a saved system.");
  }
  return core;
}

std::optional<GalaxyGenerationMetadata>
validate_galaxy_generation_metadata(
    const std::optional<GalaxyGenerationMetadata> &metadata,
    std::int64_t seed, std::span<const StellarSystem> systems) {
  if (!metadata)
    return std::nullopt;
  if (blank(metadata->entered_seed) || blank(metadata->generator_version) ||
      metadata->internal_seed != seed ||
      metadata->system_count != static_cast<int>(systems.size()) ||
      metadata->system_count <= 0 || metadata->other_civilizations < 0 ||
      metadata->guaranteed_nearby_habitable_worlds < 0)
    throw std::runtime_error(
        "Campaign generation metadata is invalid or does not match the saved galaxy.");
  if(metadata->stellar_population) (void)stellar_population_weights(*metadata->stellar_population);
  if(metadata->stellar_profile_version&&(!metadata->stellar_population||*metadata->stellar_profile_version!=stellar_population_profile_version()))
    throw std::runtime_error("Unsupported saved stellar population profile version.");
  (void)validate_galactic_core_metadata(metadata->galactic_core, systems);
  return metadata;
}

void validate_galactic_core_agreement(
    const std::optional<GalacticCoreMetadata> &metadata_core,
    const std::optional<GalacticCoreMetadata> &state_core) {
  if (metadata_core && state_core && *metadata_core != *state_core)
    throw std::runtime_error(
        "Campaign galactic-core metadata disagrees with the saved galaxy landmark.");
}

GalaxyPersistenceMetadataCapture capture_galaxy_persistence_metadata(
    const std::optional<GalaxyGenerationMetadata> &metadata,
    const std::optional<GalacticCoreMetadata> &core, std::int64_t seed,
    std::span<const StellarSystem> systems) {
  auto validated_metadata =
      validate_galaxy_generation_metadata(metadata, seed, systems);
  auto validated_core = validate_galactic_core_metadata(core, systems);
  validate_galactic_core_agreement(
      validated_metadata ? validated_metadata->galactic_core : std::nullopt,
      validated_core);
  return {std::move(validated_metadata), std::move(validated_core)};
}

} // namespace stellar::core
