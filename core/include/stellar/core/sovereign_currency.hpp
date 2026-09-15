#pragma once
#include <stellar/core/civilization_catalog.hpp>
#include <string_view>

namespace stellar::core {
struct SovereignCurrencyDefinition {
    std::string name, code, symbol;
    double local_units_per_budget_unit{};
    std::string format(double budget_units, bool include_code = true) const;
    std::string format_rate(double budget_units_per_day) const;
};
SovereignCurrencyDefinition sovereign_currency_for_species(std::string_view species_id);
SovereignCurrencyDefinition sovereign_currency_for_civilization(
    std::span<const Civilization> civilizations, int civilization_id);
} // namespace stellar::core
