#pragma once

#include <optional>
#include <span>
#include <stdexcept>
#include <string>
#include <vector>

#include <stellar/core/galaxy_catalog.hpp>
#include <stellar/core/planetary_catalog.hpp>

namespace stellar::core {

class PlanetaryBodyPersistenceDataError : public std::runtime_error {
public:
  explicit PlanetaryBodyPersistenceDataError(std::string message);
  PlanetaryBodyPersistenceDataError(std::string message,
                                    std::string inner_type,
                                    std::string inner_message);

  [[nodiscard]] const std::optional<std::string> &inner_type() const noexcept;
  [[nodiscard]] const std::optional<std::string> &inner_message() const noexcept;

private:
  std::optional<std::string> inner_type_;
  std::optional<std::string> inner_message_;
};

struct PlanetaryEnvironmentPersistenceDto {
  double gravity_g{};
  double temperature_kelvin{};
  double pressure_kpa{};
  PlanetaryAtmosphereRegime atmosphere{};
  PlanetarySolventRegime available_solvent{};
  double radiation_hazard{};
  bool is_immersed_environment{};
  bool has_solid_surface{};
};

struct PlanetaryBodyPersistenceDto {
  int id{};
  int system_id{};
  std::optional<int> parent_body_id;
  int orbit_index{};
  // System.Text.Json can materialize null despite the authored required string.
  std::optional<std::string> name;
  PlanetaryBodyKind kind{};
  double radius_earth{};
  double mass_earth{};
  std::optional<PlanetaryEnvironmentPersistenceDto> environment;
  bool legacy_colonization_candidate{};
  bool has_rare_resource{};
  bool has_anomaly{};
  bool has_pre_warp_civilization{};
  double orbital_eccentricity{};
  double orbital_inclination_degrees{};
  std::optional<StellarPlanetProperties> stellar_exposure;
  bool cracked_world{};
  std::optional<PlanetAppearance> appearance;
};

// Preserves the nullable list and nullable element distinctions accepted by the
// parser-facing source method. Values are owned and may be retained by callers.
struct PlanetaryBodyPersistenceInput {
  bool bodies_present{};
  std::vector<std::optional<PlanetaryBodyPersistenceDto>> bodies;
};

// Rejects null entries, validates the complete catalog in its authored order,
// then materializes records owning all strings and environments. DTOs are read
// in place without an intermediate full-catalog copy.
[[nodiscard]] std::vector<PlanetaryBody> restore_planetary_bodies(
    const PlanetaryBodyPersistenceInput &input,
    std::span<const StellarSystem> systems);

// Matches the CampaignSaveService capture wrapper: validate the complete
// catalog first, then project detached DTO values in input order.
[[nodiscard]] PlanetaryBodyPersistenceInput capture_planetary_bodies(
    std::span<const PlanetaryBody> bodies,
    std::span<const StellarSystem> systems);

} // namespace stellar::core
