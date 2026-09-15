#include "stellar/core/interstellar_distance.hpp"
#include <cmath>
#include <stdexcept>
namespace stellar::core {
static void validate(const StarPosition& p) {
    if (!std::isfinite(p.x) || !std::isfinite(p.y) || (p.depth_light_years && !std::isfinite(*p.depth_light_years)))
        throw std::invalid_argument("Star position must contain finite light-year coordinates");
}
double squared_distance_light_years(const StarPosition& a, const StarPosition& b) {
    validate(a); validate(b);
    if (!a.depth_light_years && !b.depth_light_years) {
        // Preserve System.Numerics.Vector2's legacy float math, not new double rules.
        const float dx = a.x - b.x, dy = a.y - b.y;
        const float result = dx * dx + dy * dy;
        return result;
    }
    const double dx = static_cast<double>(a.x) - b.x, dy = static_cast<double>(a.y) - b.y;
    const double dz = a.depth_light_years.value_or(0) - b.depth_light_years.value_or(0);
    return dx * dx + dy * dy + dz * dz;
}
double distance_light_years(const StarPosition& a, const StarPosition& b) {
    const double squared = squared_distance_light_years(a, b);
    return !a.depth_light_years && !b.depth_light_years
        ? static_cast<double>(std::sqrt(static_cast<float>(squared))) : std::sqrt(squared);
}
}
