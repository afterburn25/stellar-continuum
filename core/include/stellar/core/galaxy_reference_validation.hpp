#pragma once

#include <stellar/core/colony_economy.hpp>
#include <stellar/core/fleet_power_observation.hpp>
#include <stellar/core/fleet_state.hpp>
#include <stellar/core/massive_combat_persistence.hpp>

#include <span>
#include <stdexcept>
#include <string>

namespace stellar::core {

class GalaxyReferenceValidationDataError final : public std::runtime_error {
public:
  using std::runtime_error::runtime_error;
};

class GalaxyReferenceValidationOperationError final
    : public std::runtime_error {
public:
  using std::runtime_error::runtime_error;
};

class GalaxyReferenceValidationArgumentError final
    : public std::invalid_argument {
public:
  using std::invalid_argument::invalid_argument;
};

class GalaxyReferenceValidationRangeError final : public std::out_of_range {
public:
  using std::out_of_range::out_of_range;
};

class GalaxyReferenceValidationOverflowError final
    : public std::overflow_error {
public:
  using std::overflow_error::overflow_error;
};

struct GalaxyReferenceValidationView {
  std::span<const StellarSystem> systems;
  std::span<const PlanetaryBody> bodies;
  std::span<const Civilization> civilizations;
  std::span<const Colony> colonies;
  std::span<const CivilizationEconomy> economies;
  std::span<const FleetState> fleets;
  std::span<const FleetPowerObservation> combat_intelligence;
  CampaignMassiveEncounter *active_encounter{};
};

// Borrows the world for this call. The optional encounter is mutable because
// source battle validation materializes legacy formation ship counts.
void validate_galaxy_references(GalaxyReferenceValidationView world);

} // namespace stellar::core
