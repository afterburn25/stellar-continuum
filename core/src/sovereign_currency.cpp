#include <stellar/core/sovereign_currency.hpp>
#include <stellar/core/detail/legacy_number_format.hpp>

#include <algorithm>
#include <array>
#include <charconv>
#include <cmath>
#include <limits>
#include <stdexcept>
#include <string>

namespace stellar::core {
namespace detail {
std::string legacy_custom_fixed(double value, int minimum_fraction_digits, int maximum_fraction_digits) {
    if (minimum_fraction_digits < 0 || maximum_fraction_digits < minimum_fraction_digits || maximum_fraction_digits > 9)
        throw std::invalid_argument("Custom fixed fraction digits are invalid.");
    if (std::isnan(value)) return "NaN";
    // Invariant .NET numeric formatting spells these special values out rather
    // than using the platform's infinity glyph.
    if (std::isinf(value)) return std::signbit(value) ? "-Infinity" : "Infinity";
    if (value == 0.0) return std::signbit(value) ? "-0" + (minimum_fraction_digits ? "." + std::string(static_cast<std::size_t>(minimum_fraction_digits), '0') : "")
        : "0" + (minimum_fraction_digits ? "." + std::string(static_cast<std::size_t>(minimum_fraction_digits), '0') : "");
    const bool negative = value < 0.0;
    value = std::abs(value);
    std::array<char, 64> text{};
    // One digit before the decimal plus fourteen after it is the source's
    // precision-15 decimal working representation.
    const auto [end, error] = std::to_chars(text.data(), text.data() + text.size(), value,
        std::chars_format::scientific, 14);
    if (error != std::errc{}) throw std::runtime_error("Currency amount is too large to format.");
    std::string scientific{text.data(), end};
    const auto marker = scientific.find_first_of("eE");
    const int exponent = std::stoi(scientific.substr(marker + 1));
    std::string digits = scientific.substr(0, marker);
    if (digits.starts_with('-')) digits.erase(0, 1);
    const auto point = digits.find('.');
    const std::size_t before_point = point == std::string::npos ? digits.size() : point;
    if (point != std::string::npos) digits.erase(point, 1);
    const auto decimal_position = static_cast<long long>(before_point) + exponent;
    std::string result;
    if (decimal_position >= static_cast<long long>(digits.size()))
        result = digits + std::string(static_cast<std::size_t>(decimal_position - digits.size()), '0');
    else if (decimal_position > 0)
        result = digits.substr(0, static_cast<std::size_t>(decimal_position)) + "." + digits.substr(static_cast<std::size_t>(decimal_position));
    else
        result = "0." + std::string(static_cast<std::size_t>(-decimal_position), '0') + digits;
    auto decimal = result.find('.');
    if (decimal != std::string::npos && result.size() > decimal + static_cast<std::size_t>(maximum_fraction_digits + 1)) {
        const bool round_up = result[decimal + maximum_fraction_digits + 1] >= '5';
        result.erase(decimal + maximum_fraction_digits + 1);
        if (round_up) {
            for (auto cursor = result.size(); cursor-- > 0;) {
                if (result[cursor] == '.') continue;
                if (result[cursor] != '9') { ++result[cursor]; break; }
                result[cursor] = '0';
                if (cursor == 0) result.insert(result.begin(), '1');
            }
        }
    }
    if ((decimal = result.find('.')) != std::string::npos) {
        const auto minimum_length = decimal + 1 + static_cast<std::size_t>(minimum_fraction_digits);
        while (result.size() > minimum_length && result.back() == '0') result.pop_back();
        if (result.back() == '.') result.pop_back();
    }
    decimal = result.find('.');
    if (minimum_fraction_digits) {
        if (decimal == std::string::npos) result += "." + std::string(static_cast<std::size_t>(minimum_fraction_digits), '0');
        else if (result.size() - decimal - 1 < static_cast<std::size_t>(minimum_fraction_digits))
            result += std::string(static_cast<std::size_t>(minimum_fraction_digits) - (result.size() - decimal - 1), '0');
    }
    return negative ? "-" + result : result;
}
} // namespace detail

namespace {
const SovereignCurrencyDefinition& unknown_currency() {
    static const SovereignCurrencyDefinition value{"Sovereign Currency", "SC", "¤", 10'000'000.0};
    return value;
}
const std::array<SovereignCurrencyDefinition, 4>& currencies() {
    static const std::array<SovereignCurrencyDefinition, 4> values{{
        {"United Earth Dollar", "UED", "$", 10'000'000.0},
        {"Tide Mark", "TM", "◈", 12'000'000.0},
        {"Forge Crown", "FC", "◆", 8'000'000.0},
        {"Thermal Ledger", "TL", "◇", 15'000'000.0},
    }};
    return values;
}
}

std::string SovereignCurrencyDefinition::format(double budget_units, bool include_code) const {
    // Math.Max(0, NaN) preserves NaN.  Negative infinity and ordinary negatives
    // are clamped before scaling, exactly as the source display does.
    const double clamped = std::isnan(budget_units) ? budget_units : std::max(0.0, budget_units);
    const double local = clamped * local_units_per_budget_unit;
    double amount = local;
    std::string_view suffix;
    if (local >= 1'000'000'000'000.0) { amount = local / 1'000'000'000'000.0; suffix = "T"; }
    else if (local >= 1'000'000'000.0) { amount = local / 1'000'000'000.0; suffix = "B"; }
    else if (local >= 1'000'000.0) { amount = local / 1'000'000.0; suffix = "M"; }
    else if (local >= 1'000.0) { amount = local / 1'000.0; suffix = "K"; }
    std::string result = symbol + detail::legacy_custom_fixed(amount, 0, 2) + std::string(suffix);
    if (include_code) result += " " + code;
    return result;
}

std::string SovereignCurrencyDefinition::format_rate(double budget_units_per_day) const {
    const std::string sign = budget_units_per_day < 0.0 ? "−" : budget_units_per_day > 0.0 ? "+" : "";
    return sign + format(std::abs(budget_units_per_day)) + "/day";
}

SovereignCurrencyDefinition sovereign_currency_for_species(std::string_view species_id) {
    if (species_id == "terran_baseline") return currencies()[0];
    if (species_id == "pelagic_high_pressure") return currencies()[1];
    if (species_id == "compact_high_gravity") return currencies()[2];
    if (species_id == "cryogenic_hydrocarbon") return currencies()[3];
    return unknown_currency();
}

SovereignCurrencyDefinition sovereign_currency_for_civilization(
    std::span<const Civilization> civilizations, int civilization_id) {
    const auto found = std::find_if(civilizations.begin(), civilizations.end(), [=](const Civilization& civilization) {
        return civilization.id == civilization_id;
    });
    if (found == civilizations.end())
        throw std::out_of_range("Civilization " + std::to_string(civilization_id) + " does not exist.");
    return sovereign_currency_for_species(found->species_id);
}
} // namespace stellar::core
