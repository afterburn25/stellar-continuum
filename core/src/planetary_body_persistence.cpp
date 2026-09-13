#include <stellar/core/planetary_body_persistence.hpp>

#include <cmath>
#include <cstdint>
#include <string_view>
#include <unordered_map>
#include <unordered_set>
#include <utility>

namespace stellar::core {
namespace {

bool valid(PlanetaryBodyKind value) noexcept {
  return value >= PlanetaryBodyKind::Planet &&
         value <= PlanetaryBodyKind::DwarfPlanet;
}

bool valid(PlanetaryAtmosphereRegime value) noexcept {
  return value >= PlanetaryAtmosphereRegime::Vacuum &&
         value <= PlanetaryAtmosphereRegime::Other;
}

bool valid(PlanetarySolventRegime value) noexcept {
  return value >= PlanetarySolventRegime::None &&
         value <= PlanetarySolventRegime::Other;
}

bool environment_valid(const PlanetaryBodyPersistenceDto &body) noexcept {
  return body.environment && valid(body.environment->atmosphere) &&
         valid(body.environment->available_solvent);
}

bool environment_valid(const PlanetaryBody &body) noexcept {
  return valid(body.environment.atmosphere) &&
         valid(body.environment.available_solvent);
}
PlanetaryBody materialize(const PlanetaryBodyPersistenceDto &body) {
  const auto &environment = *body.environment;
  return {
      body.id,
      body.system_id,
      body.parent_body_id,
      body.orbit_index,
      body.name.value_or(""),
      body.kind,
      body.radius_earth,
      body.mass_earth,
      {environment.gravity_g,
       environment.temperature_kelvin,
       environment.pressure_kpa,
       environment.atmosphere,
       environment.available_solvent,
       environment.radiation_hazard,
       environment.is_immersed_environment,
       environment.has_solid_surface},
      body.legacy_colonization_candidate,
      body.has_rare_resource,
      body.has_anomaly,
      body.has_pre_warp_civilization,
      body.orbital_eccentricity,
      body.orbital_inclination_degrees,
  };
}

void validate_physical(const PlanetaryBody &body) {
  try {
    validate_planetary_body(body);
  } catch (const std::invalid_argument &error) {
    throw PlanetaryBodyPersistenceDataError(
        "Planetary body " + std::to_string(body.id) +
            " has invalid physical values.",
        "InvalidOperationException", error.what());
  }
}

void validate_physical(const PlanetaryBodyPersistenceDto &body) {
  validate_physical(materialize(body));
}
PlanetaryBodyPersistenceDto project(const PlanetaryBody &body) {
  return {
      body.id,
      body.system_id,
      body.parent_body_id,
      body.orbit_index,
      body.name,
      body.kind,
      body.radius_earth,
      body.mass_earth,
      PlanetaryEnvironmentPersistenceDto{
          body.environment.gravity_g,
          body.environment.temperature_kelvin,
          body.environment.pressure_kpa,
          body.environment.atmosphere,
          body.environment.available_solvent,
          body.environment.radiation_hazard,
          body.environment.is_immersed_environment,
          body.environment.has_solid_surface,
      },
      body.legacy_colonization_candidate,
      body.has_rare_resource,
      body.has_anomaly,
      body.has_pre_warp_civilization,
      body.orbital_eccentricity,
      body.orbital_inclination_degrees,
  };
}

template <typename Body>
void validate_catalog(std::span<const Body> bodies,
                      std::span<const StellarSystem> systems) {
  if (bodies.empty())
    throw PlanetaryBodyPersistenceDataError(
        "The authoritative planetary catalog is missing or empty.");

  std::unordered_set<int> distinct_ids;
  for (const auto &body : bodies)
    distinct_ids.insert(body.id);
  if (distinct_ids.size() != bodies.size())
    throw PlanetaryBodyPersistenceDataError(
        "The authoritative planetary catalog contains duplicate body IDs.");

  std::unordered_set<int> system_ids;
  for (const auto &system : systems)
    system_ids.insert(system.id);
  std::unordered_map<int, const Body *> by_id;
  for (const auto &body : bodies)
    by_id.emplace(body.id, &body);

  for (const auto &body : bodies) {
    if (!valid(body.kind))
      throw PlanetaryBodyPersistenceDataError(
          "Planetary body " + std::to_string(body.id) +
          " has an unknown body kind.");
    if (!environment_valid(body))
      throw PlanetaryBodyPersistenceDataError(
          "Planetary body " + std::to_string(body.id) +
          " has an invalid environment.");

    validate_physical(body);

    if (!system_ids.contains(body.system_id))
      throw PlanetaryBodyPersistenceDataError(
          "Planetary body " + std::to_string(body.id) +
          " references unknown system " + std::to_string(body.system_id) +
          ".");

    std::unordered_set<int> visited{body.id};
    auto *ancestor = &body;
    while (ancestor->parent_body_id) {
      const auto ancestor_id = *ancestor->parent_body_id;
      if (!visited.insert(ancestor_id).second)
        throw PlanetaryBodyPersistenceDataError(
            "Planetary body " + std::to_string(body.id) +
            " belongs to a cyclic parent chain.");
      const auto found = by_id.find(ancestor_id);
      if (found == by_id.end())
        break;
      ancestor = found->second;
    }

    if ((body.kind == PlanetaryBodyKind::Planet ||
         body.kind == PlanetaryBodyKind::DwarfPlanet) &&
        body.parent_body_id)
      throw PlanetaryBodyPersistenceDataError(
          "Planetary body " + std::to_string(body.id) +
          " is a primary body with a parent body.");
    if (body.kind == PlanetaryBodyKind::Moon && !body.parent_body_id)
      throw PlanetaryBodyPersistenceDataError(
          "Planetary body " + std::to_string(body.id) +
          " is a moon without a parent planet.");
    if (body.parent_body_id) {
      const auto found = by_id.find(*body.parent_body_id);
      if (found == by_id.end() ||
          found->second->kind != PlanetaryBodyKind::Planet ||
          found->second->system_id != body.system_id)
        throw PlanetaryBodyPersistenceDataError(
            "Planetary body " + std::to_string(body.id) +
            " has an invalid or cross-system parent.");
    }
  }
}

} // namespace

PlanetaryBodyPersistenceDataError::PlanetaryBodyPersistenceDataError(
    std::string message)
    : std::runtime_error(std::move(message)) {}

PlanetaryBodyPersistenceDataError::PlanetaryBodyPersistenceDataError(
    std::string message, std::string inner_type, std::string inner_message)
    : std::runtime_error(std::move(message)),
      inner_type_(std::move(inner_type)),
      inner_message_(std::move(inner_message)) {}

const std::optional<std::string> &
PlanetaryBodyPersistenceDataError::inner_type() const noexcept {
  return inner_type_;
}

const std::optional<std::string> &
PlanetaryBodyPersistenceDataError::inner_message() const noexcept {
  return inner_message_;
}

std::vector<PlanetaryBody> restore_planetary_bodies(
    const PlanetaryBodyPersistenceInput &input,
    std::span<const StellarSystem> systems) {
  if (!input.bodies_present || input.bodies.empty())
    throw PlanetaryBodyPersistenceDataError(
        "Format v16 save is missing its authoritative planetary catalog.");
  for (const auto &body : input.bodies)
    if (!body)
      throw PlanetaryBodyPersistenceDataError(
          "Format v16 planetary catalog contains a null body entry.");

  std::vector<PlanetaryBodyPersistenceDto> materialized;
  materialized.reserve(input.bodies.size());
  for (const auto &body : input.bodies)
    materialized.push_back(*body);

  validate_catalog(
      std::span<const PlanetaryBodyPersistenceDto>(materialized), systems);
  std::vector<PlanetaryBody> result;
  result.reserve(materialized.size());
  for (const auto &body : materialized)
    result.push_back(materialize(body));
  return result;
}

PlanetaryBodyPersistenceInput capture_planetary_bodies(
    std::span<const PlanetaryBody> bodies,
    std::span<const StellarSystem> systems) {
  validate_catalog(bodies, systems);

  std::vector<PlanetaryBodyPersistenceDto> values;
  values.reserve(bodies.size());
  for (const auto &body : bodies)
    values.push_back(project(body));

  PlanetaryBodyPersistenceInput result{true, {}};
  result.bodies.reserve(values.size());
  for (auto &body : values)
    result.bodies.emplace_back(std::move(body));
  return result;
}

} // namespace stellar::core
