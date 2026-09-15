#pragma once
#include <array>
#include <cstdint>
namespace stellar::core {
// Compatibility with explicitly seeded System.Random on the preserved .NET 8 game.
// Keep distinct from the Engine SplitMix64 utility; changing streams changes catalogs.
class LegacyRandom {
public:
    explicit LegacyRandom(std::int32_t seed);
    int next();
    int next(int exclusive_max);
    double next_double();
private:
    std::array<int,56> seed_array_{};
    int inext_{}, inextp_{21};
};
std::int32_t population_seed(std::int64_t seed, std::int32_t salt);
}
