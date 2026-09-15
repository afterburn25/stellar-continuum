#pragma once
#include <optional>
namespace stellar::core {
struct StarPosition { float x{}, y{}; std::optional<double> depth_light_years; };
double squared_distance_light_years(const StarPosition& first, const StarPosition& second);
double distance_light_years(const StarPosition& first, const StarPosition& second);
}
