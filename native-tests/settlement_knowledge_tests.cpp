#include <stellar/core/settlement_knowledge.hpp>

#include <nlohmann/json.hpp>

#include <algorithm>
#include <cmath>
#include <exception>
#include <fstream>
#include <iostream>
#include <limits>
#include <optional>
#include <stdexcept>
#include <string>
#include <vector>

using Json = nlohmann::json;
using namespace stellar::core;
namespace {
[[noreturn]] void fail(const std::string &m) { throw std::runtime_error(m); }
void check(bool c, const std::string &m) {
  if (!c)
    fail(m);
}
double number(const Json &v) {
  if (v.is_number())
    return v.get<double>();
  const auto s = v.get<std::string>();
  if (s == "NaN")
    return std::numeric_limits<double>::quiet_NaN();
  if (s == "Infinity")
    return std::numeric_limits<double>::infinity();
  if (s == "-Infinity")
    return -std::numeric_limits<double>::infinity();
  fail("bad number " + s);
}
template <class T> std::optional<T> optional(const Json &v) {
  return v.is_null() ? std::nullopt : std::optional<T>(v.get<T>());
}
void equal_json(const Json &a, const Json &e, const std::string &f) {
  if (a.is_number() && e.is_number()) {
    const auto x = a.get<double>(), y = e.get<double>();
    const auto scale = std::max({1.0, std::abs(x), std::abs(y)});
    check(std::abs(x - y) <= 1e-7 * scale, f + ": number mismatch");
    return;
  }
  if (a.is_array() && e.is_array()) {
    check(a.size() == e.size(), f + ": size");
    for (std::size_t i = 0; i < a.size(); ++i)
      equal_json(a[i], e[i], f + "[" + std::to_string(i) + "]");
    return;
  }
  if (a.is_object() && e.is_object()) {
    check(a.size() == e.size(), f + ": object size");
    for (const auto &[k, v] : e.items()) {
      check(a.contains(k), f + ": missing " + k);
      equal_json(a.at(k), v, f + "." + k);
    }
    return;
  }
  check(a == e, f + ": mismatch actual=" + a.dump() + " expected=" + e.dump());
}

StellarSystem parse_system(const Json &v) {
  StellarSystem r;
  r.id = v.at("Id");
  r.name = v.at("Name");
  r.position = {static_cast<float>(number(v.at("Position").at("X"))),
                static_cast<float>(number(v.at("Position").at("Y"))),
                optional<double>(v.at("GalacticDepthLightYears"))};
  return r;
}
PlanetaryBody parse_body(const Json &v) {
  PlanetaryBody r;
  r.id = v.at("Id");
  r.system_id = v.at("SystemId");
  r.parent_body_id = optional<int>(v.at("ParentBodyId"));
  r.orbit_index = v.at("OrbitIndex");
  r.name = v.at("Name");
  r.kind = static_cast<PlanetaryBodyKind>(v.at("Kind").get<int>());
  r.radius_earth = number(v.at("RadiusEarth"));
  r.mass_earth = number(v.at("MassEarth"));
  const auto &e = v.at("Environment");
  r.environment = {
      number(e.at("GravityG")),
      number(e.at("TemperatureKelvin")),
      number(e.at("PressureKPa")),
      static_cast<PlanetaryAtmosphereRegime>(e.at("Atmosphere").get<int>()),
      static_cast<PlanetarySolventRegime>(e.at("AvailableSolvent").get<int>()),
      number(e.at("RadiationHazard")),
      e.at("IsImmersedEnvironment"),
      e.at("HasSolidSurface")};
  r.legacy_colonization_candidate = v.at("LegacyColonizationCandidate");
  r.has_rare_resource = v.at("HasRareResource");
  r.has_anomaly = v.at("HasAnomaly");
  r.has_pre_warp_civilization = v.at("HasPreWarpCivilization");
  if (v.contains("OrbitalEccentricity"))
    r.orbital_eccentricity = number(v.at("OrbitalEccentricity"));
  if (v.contains("OrbitalInclinationDegrees"))
    r.orbital_inclination_degrees = number(v.at("OrbitalInclinationDegrees"));
  return r;
}
Civilization parse_civ(const Json &v) {
  Civilization r;
  r.id = v.at("Id");
  r.name = v.at("Name");
  r.home_system_id = v.at("HomeSystemId");
  r.species_id = v.at("SpeciesId");
  return r;
}
Colony parse_colony(const Json &v) {
  Colony r;
  r.id = v.at("Id");
  r.civilization_id = v.at("CivilizationId");
  r.system_id = v.at("SystemId");
  r.planetary_body_id = optional<int>(v.at("PlanetaryBodyId"));
  r.name = v.at("Name");
  r.kind = static_cast<SettlementKind>(v.at("Kind").get<int>());
  r.population_species_id = v.at("PopulationSpeciesId");
  r.population_millions = number(v.at("PopulationMillions"));
  return r;
}
FleetState parse_fleet(const Json &v) {
  FleetState r;
  r.id = v.at("Id");
  r.civilization_id = v.at("CivilizationId");
  r.name = v.at("Name");
  r.role = static_cast<FleetRole>(v.at("Role").get<int>());
  r.current_system_id = optional<int>(v.at("CurrentSystemId"));
  r.destination_system_id = optional<int>(v.at("DestinationSystemId"));
  r.destination_planetary_body_id =
      optional<int>(v.at("DestinationPlanetaryBodyId"));
  r.hold_requested = v.at("HoldRequested");
  r.prevent_automatic_settlement = v.at("PreventAutomaticSettlement");
  r.embarked_population_millions = number(v.at("EmbarkedPopulationMillions"));
  r.embarked_population_species_id =
      optional<std::string>(v.at("EmbarkedPopulationSpeciesId"));
  r.is_active = v.at("IsActive");
  return r;
}
struct World {
  std::vector<StellarSystem> systems;
  std::vector<PlanetaryBody> bodies;
  std::vector<Civilization> civs;
  std::vector<Colony> colonies;
  std::vector<FleetState> fleets;
  CivilizationKnowledgeState knowledge;
  SettlementKnowledgeWorldView view() const {
    return {systems, bodies, civs, colonies, fleets, knowledge};
  }
};
World parse_world(const Json &v) {
  World r;
  for (const auto &e : v.at("Systems"))
    r.systems.push_back(parse_system(e));
  for (const auto &e : v.at("Bodies"))
    r.bodies.push_back(parse_body(e));
  for (const auto &e : v.at("Civilizations"))
    r.civs.push_back(parse_civ(e));
  for (const auto &e : v.at("Colonies"))
    r.colonies.push_back(parse_colony(e));
  for (const auto &e : v.at("Fleets"))
    r.fleets.push_back(parse_fleet(e));
  for (const auto &o : v.at("Knowledge")) {
    const auto id = o.at("CivilizationId").get<int>();
    for (const auto &s : o.at("Surveys")) {
      const auto sid = s.at("SystemId").get<int>(),
                 level = s.at("Level").get<int>();
      const auto p = number(s.at("Progress"));
      if (level == 1)
        r.knowledge.reveal_system(id, sid);
      else if (level == 2)
        r.knowledge.advance_system_survey(id, sid, p);
      else if (level == 3)
        r.knowledge.mark_system_fully_surveyed(id, sid);
      else
        fail("unsupported survey level");
    }
  }
  return r;
}
Json encode_view(const KnownSpeciesPlanetarySuitability &v) {
  return {
      {"PlanetaryBodyId", v.planetary_body_id},
      {"SystemId", v.system_id},
      {"SpeciesId", v.species_id},
      {"NaturalHabitability", v.natural_habitability},
      {"UnprotectedOperationalCapacity", v.unprotected_operational_capacity},
      {"LimitingFactor", v.limiting_factor},
      {"ColonizationViability", v.colonization_viability},
      {"RequiresGravityMitigation", v.requires_gravity_mitigation},
      {"RequiresThermalControl", v.requires_thermal_control},
      {"RequiresPressureControl", v.requires_pressure_control},
      {"RequiresSealedHabitat", v.requires_sealed_habitat},
      {"RequiresArtificialBiosphere", v.requires_artificial_biosphere},
      {"RequiresRadiationShielding", v.requires_radiation_shielding}};
}
struct Error {
  std::string type, message;
};
Error classify(const std::exception &e) {
  if (dynamic_cast<const std::invalid_argument *>(&e))
    return {"ArgumentException", e.what()};
  if (dynamic_cast<const std::out_of_range *>(&e))
    return {"KeyNotFoundException", e.what()};
  if (dynamic_cast<const std::runtime_error *>(&e))
    return {"InvalidOperationException", e.what()};
  return {"UnexpectedNativeException", e.what()};
}
void run_case(const Json &t) {
  const auto name = t.at("Name").get<std::string>(),
             kind = t.at("Kind").get<std::string>();
  check(kind == "BuildForSpecies" || kind == "BuildForAvailablePopulations" ||
            kind == "ResolveBestAvailableBody" || kind == "BuildReservations" ||
            kind == "TryReservation",
        name + ": unknown kind");
  const auto args = t.at("Arguments"), input = args.at("World"),
             before = t.at("Before"), after = t.at("After"),
             expected = t.at("Result"), expected_error = t.at("Error");
  const auto observer = args.at("ObserverCivilizationId").get<int>(),
             system = args.at("SystemId").get<int>();
  const auto species = optional<std::string>(args.at("SpeciesId"));
  if (kind == "BuildForSpecies" || kind == "ResolveBestAvailableBody")
    check(species.has_value(), name + ": missing species ID");
  const auto requesting_json = args.at("RequestingFleet");
  check(input == before, name + ": input/before");
  check(after == before, name + ": read helper mutated source world");
  if (!expected_error.is_null())
    check(expected.is_null(), name + ": error result");
  auto world = parse_world(input);
  std::optional<Error> error;
  std::optional<FleetState> requesting;
  if (!requesting_json.is_null())
    requesting = parse_fleet(requesting_json);
  if (kind == "BuildReservations" || kind == "TryReservation")
    check(requesting.has_value(), name + ": missing requesting fleet");
  std::optional<std::vector<KnownSpeciesPlanetarySuitability>> suitability;
  std::optional<std::optional<int>> body_id;
  std::optional<std::vector<FriendlyColonyMissionReservation>> reservations;
  std::optional<FriendlyColonyReservationLookup> lookup;
  try {
    if (kind == "BuildForSpecies")
      suitability = build_known_suitability_for_species(
          world.view(), observer, species ? *species : std::string{});
    else if (kind == "BuildForAvailablePopulations")
      suitability = build_known_suitability_for_available_populations(
          world.view(), observer);
    else if (kind == "ResolveBestAvailableBody")
      body_id = resolve_best_available_settlement_body(
          world.view(), observer, system, species ? *species : std::string{});
    else if (kind == "BuildReservations")
      reservations =
          build_friendly_colony_mission_reservations(world.view(), *requesting);
    else
      lookup = try_get_friendly_reserving_fleet_id(world.view(), *requesting,
                                                   system);
  } catch (const std::exception &e) {
    error = classify(e);
  }
  check(error.has_value() == !expected_error.is_null(),
        name + ": error presence" +
            (error ? " " + error->type + ": " + error->message : ""));
  if (error) {
    check(error->type == expected_error.at("Type").get<std::string>(),
          name + ": error type");
    check(error->message == expected_error.at("Message").get<std::string>(),
          name + ": error message actual=" + error->message);
  } else {
    Json result;
    if (suitability) {
      result = Json::array();
      for (const auto &value : *suitability)
        result.push_back(encode_view(value));
    } else if (body_id) {
      result = *body_id ? Json(**body_id) : Json(nullptr);
    } else if (reservations) {
      result = Json::array();
      for (const auto &value : *reservations)
        result.push_back(
            {{"SystemId", value.system_id}, {"FleetId", value.fleet_id}});
    } else {
      check(lookup.has_value(), name + ": missing typed result");
      result = {{"Found", lookup->found},
                {"ReservingFleetId", lookup->reserving_fleet_id}};
    }
    equal_json(result, expected, name + ".Result");
  }
}
void source_nulls(const Json &galaxy, const Json &fleet, const Json &species) {
  const auto check_values = [](const Json &values, std::size_t count,
                               std::string_view parameter) {
    check(values.size() == count, "source-only null count");
    for (const auto &entry : values) {
      check(entry.at("Error").at("Type") == "ArgumentNullException",
            "source-only null type");
      check(entry.at("Error").at("Message") ==
                "Value cannot be null. (Parameter '" + std::string(parameter) +
                    "')",
            "source-only null message");
    }
  };
  check_values(galaxy, 5, "galaxy");
  check_values(fleet, 2, "requestingFleet");
  check_values(species, 2, "key");
}
} // namespace
int main(int argc, char **argv) {
  try {
    check(argc == 2, "usage: settlement_knowledge_tests fixture");
    std::ifstream s(argv[1]);
    check(s.good(), "fixture open");
    const auto f = Json::parse(s);
    check(f.at("Format") == "stellar-settlement-knowledge-oracle-v1", "schema");
    check(f.at("NativeBoundary") == "Typed native references cannot represent "
                                    "null galaxy or requesting fleet.",
          "boundary");
    source_nulls(f.at("SourceOnlyNullGalaxy"),
                 f.at("SourceOnlyNullRequestingFleet"),
                 f.at("SourceOnlyNullSpecies"));
    std::size_t passed = 0;
    for (const auto &t : f.at("Cases")) {
      run_case(t);
      ++passed;
    }
    std::cout << "settlement knowledge parity passed " << passed
              << " native actual-C# cases; retained 9 source-only null "
                 "observations\n";
    return 0;
  } catch (const std::exception &e) {
    std::cerr << "settlement knowledge parity failed: " << e.what() << '\n';
    return 1;
  }
}
