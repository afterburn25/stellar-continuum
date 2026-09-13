#pragma once
#include <string>

namespace stellar::core::detail {
// Preserved invariant .NET 8 custom fixed-point formatting for simulation messages.
// This is a narrow compatibility helper, not the future localized player UI.
std::string legacy_custom_fixed(double value, int minimum_fraction_digits, int maximum_fraction_digits);
} // namespace stellar::core::detail
