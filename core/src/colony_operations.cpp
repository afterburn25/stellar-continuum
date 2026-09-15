#include <stellar/core/colony_operations.hpp>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <stdexcept>

namespace stellar::core {
namespace {
constexpr double sealed_hub_storage_capacity = 25.0;
constexpr double storage_capacity_per_fabrication_complex = 100.0;
constexpr double minimum_deposit_reserve = 3000.0;
constexpr double maximum_deposit_reserve = 20000.0;

const PlanetaryBody* settlement_body(std::span<const PlanetaryBody> bodies, const Colony& colony) {
    if (!colony.planetary_body_id) return nullptr;
    for (const auto& body : bodies)
        if (body.id == *colony.planetary_body_id && body.system_id == colony.system_id) return &body;
    return nullptr;
}
ResourceDepositProfile empty_deposit() { return {"No confirmed deposit", "None", 0, 0, 0, 0}; }
double round_away_from_zero_2(double value) {
    return std::round(value * 100.0) / 100.0;
}
}

ResourceDepositProfile resource_deposit_profile(const PlanetaryBody& body) {
    if (!body.has_rare_resource) return empty_deposit();
    std::uint32_t hash = static_cast<std::uint32_t>(static_cast<std::uint32_t>(body.id) * 747796405u + 2891336453u);
    hash = (hash >> (static_cast<int>(hash >> 28) + 4)) ^ hash;
    hash *= 277803737u;
    hash = (hash >> 22) ^ hash;
    const auto material = body.environment.temperature_kelvin < 190.0 ? "Volatile ices" :
        (hash % 5u == 0 ? "Nickel-iron ore" : hash % 5u == 1 ? "Cobalt-rich ore" :
         hash % 5u == 2 ? "Platinum-group ore" : hash % 5u == 3 ? "Rare-earth minerals" : "Uranium-thorium ore");
    const double grade_multiplier = .70 + static_cast<double>((hash >> 8) & 0xffu) / 255.0 * .70;
    const auto grade = grade_multiplier >= 1.22 ? "Exceptional" : grade_multiplier >= 1.04 ? "Rich" :
        grade_multiplier >= .86 ? "Standard" : "Marginal";
    const auto& environment = body.environment;
    const double hazard = std::clamp(environment.radiation_hazard * .45 + std::abs(environment.gravity_g - 1.0) * .12 +
        (environment.pressure_kpa <= .01 ? .12 : std::max(0.0, environment.pressure_kpa - 180.0) / 2000.0) +
        std::abs(environment.temperature_kelvin - 288.0) / 1000.0, 0.0, .75);
    const double variation = .90 + static_cast<double>((hash >> 16) & 0xffu) / 255.0 * .10;
    const double accessibility = std::clamp((1.0 - hazard) * variation, .45, 1.0);
    return {material, grade, grade_multiplier, accessibility, hazard, grade_multiplier * accessibility};
}

double initial_deposit_reserve(const PlanetaryBody& body) {
    if (!body.has_rare_resource) return 0;
    return std::clamp(minimum_deposit_reserve + body.radius_earth * 2500.0 + std::sqrt(body.mass_earth) * 1000.0,
        minimum_deposit_reserve, maximum_deposit_reserve);
}

ResourceOutpostOperationsSnapshot resource_outpost_snapshot(std::span<const PlanetaryBody> bodies,
    std::span<const CivilizationEconomy> economies, const Colony& settlement, std::optional<double> operating_funding_fraction) {
    if (settlement.kind != SettlementKind::ResourceOutpost)
        return {false, false, 0, 0, 0, 0, 0, "No confirmed deposit", "None", 0, 0, "Ordinary colony"};
    const auto* body = settlement_body(bodies, settlement);
    const bool has_deposit = body && body->has_rare_resource;
    const auto deposit = body ? resource_deposit_profile(*body) : empty_deposit();
    const double initial = body ? initial_deposit_reserve(*body) : 0;
    const double remaining = has_deposit ? settlement.remaining_extractable_materials.value_or(std::max(0.0, initial - settlement.stored_extracted_materials)) : 0;
    if (!std::isfinite(remaining) || remaining < 0) throw std::invalid_argument("Resource outpost has an invalid remaining deposit reserve.");
    const auto surface = surface_colony_output(settlement);
    int fabricators = 0;
    for (const auto& building : settlement.surface_buildings)
        if (building.is_complete && surface_functional_family(building.type_id) == "fabricator") ++fabricators;
    const double capacity = sealed_hub_storage_capacity + fabricators * storage_capacity_per_fabrication_complex;
    double funding = 1;
    if (operating_funding_fraction) funding = *operating_funding_fraction;
    else for (const auto& economy : economies) if (economy.civilization_id == settlement.civilization_id) { funding = economy.last_base_operations_funding_fraction; break; }
    if (!std::isfinite(funding) || funding < 0 || funding > 1) throw std::out_of_range("Operating funding fraction must be finite and between zero and one.");
    const double extraction = has_deposit && remaining > .000001 ? surface.industry_per_day * funding * deposit.extraction_yield_multiplier : 0;
    std::string status;
    if (!has_deposit) status = "No confirmed extractable deposit";
    else if (remaining <= .000001) status = "Deposit depleted: no extractable material remains";
    else if (fabricators == 0) status = "Build a fabrication complex to begin extraction";
    else if (surface.industry_per_day > 0 && funding < .999999) {
        const double percentage = funding * 100.0;
        // C# invariant 0% format suppresses all fractional digits and rounds midpoint away from zero.
        status = "Extraction running at " + std::to_string(static_cast<long long>(std::floor(percentage + .5 + 1e-12))) + "% operating funding";
    } else if (extraction <= 0) status = "Extraction offline: processing complex lacks power";
    else if (settlement.stored_extracted_materials + .0001 >= capacity) status = "Storage full: freight service required";
    else status = "Extracting to local storage; freight service not yet established";
    return {true, has_deposit, extraction, settlement.stored_extracted_materials, capacity, remaining, initial,
        deposit.material_name, deposit.grade, deposit.accessibility, deposit.extraction_yield_multiplier, status};
}

void advance_resource_outpost(std::span<const PlanetaryBody> bodies, std::span<const CivilizationEconomy> economies,
    Colony& settlement, double simulation_days, double operating_funding_fraction) {
    if (settlement.kind != SettlementKind::ResourceOutpost || simulation_days <= 0) return;
    if (!std::isfinite(operating_funding_fraction) || operating_funding_fraction < 0 || operating_funding_fraction > 1)
        throw std::out_of_range("Operating funding fraction must be finite and between zero and one.");
    const auto snapshot = resource_outpost_snapshot(bodies, economies, settlement, operating_funding_fraction);
    if (!snapshot.has_confirmed_deposit) return;
    const double free_storage = std::max(0.0, snapshot.storage_capacity - settlement.stored_extracted_materials);
    const double extracted = std::min(snapshot.remaining_deposit_materials, std::min(free_storage, snapshot.extraction_per_day * simulation_days));
    settlement.stored_extracted_materials += extracted;
    settlement.remaining_extractable_materials = std::max(0.0, snapshot.remaining_deposit_materials - extracted);
}

double surface_environmental_wear(std::span<const PlanetaryBody> bodies, const Colony& colony) {
    const auto* body = settlement_body(bodies, colony);
    if (!body) return 1;
    const auto& e = body->environment;
    const double gravity = std::min(.45, std::abs(e.gravity_g - 1.0) * .35);
    const double atmosphere = e.atmosphere == PlanetaryAtmosphereRegime::Vacuum ? .30 : (e.pressure_kpa < 20 || e.pressure_kpa > 300 ? .20 : 0);
    const double thermal = e.temperature_kelvin < 240 ? std::min(.45, (240 - e.temperature_kelvin) / 240) :
        (e.temperature_kelvin > 330 ? std::min(.45, (e.temperature_kelvin - 330) / 240) : 0);
    const double radiation = std::min(.60, std::max(0.0, e.radiation_hazard - .10) * 1.5);
    return round_away_from_zero_2(std::clamp(1.0 + gravity + atmosphere + thermal + radiation, 1.0, 3.0));
}

void advance_surface_condition(std::span<const PlanetaryBody> bodies, Colony& colony, double funding_fraction, double simulation_days) {
    if (!std::isfinite(funding_fraction) || funding_fraction < 0 || funding_fraction > 1 || !std::isfinite(simulation_days) || simulation_days < 0)
        throw std::out_of_range("Surface maintenance requires finite elapsed days and a funding fraction from zero to one.");
    if (simulation_days <= 0 || funding_fraction >= 1.0 - .0000001) return;
    const double loss = .002 * surface_environmental_wear(bodies, colony) * (1 - funding_fraction) * simulation_days;
    for (auto& building : colony.surface_buildings) if (building.is_complete && building.is_enabled)
        building.condition = std::max(0.0, building.condition - loss);
}
} // namespace stellar::core
