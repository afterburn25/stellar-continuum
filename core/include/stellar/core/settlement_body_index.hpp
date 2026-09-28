#pragma once
#include <stellar/core/colony_biology.hpp>
#include <cstdint>
#include <unordered_map>

namespace stellar::core {
// Request-scoped lookup: body storage and colony identities must remain stable
// for its lifetime. Missing bodies stay missing; canonical rules still decide
// whether absence is an error or a supported legacy fallback.
class SettlementBodyIndex {
public:
    SettlementBodyIndex(std::span<const Colony> colonies, std::span<const PlanetaryBody> bodies) {
        bodies_.reserve(colonies.size());
        for (const auto& colony : colonies)
            if (colony.planetary_body_id) bodies_.try_emplace(key(colony.system_id, *colony.planetary_body_id), nullptr);
        auto remaining = bodies_.size();
        if (!remaining) return;
        for (const auto& body : bodies) {
            const auto found = bodies_.find(key(body.system_id, body.id));
            if (found != bodies_.end() && !found->second) {
                found->second = &body; // Preserve the first matching body in source order.
                if (--remaining == 0) break;
            }
        }
    }
    std::span<const PlanetaryBody> bodies_for(const Colony& colony) const {
        if (!colony.planetary_body_id) return {};
        const auto found = bodies_.find(key(colony.system_id, *colony.planetary_body_id));
        return found == bodies_.end() || !found->second
            ? std::span<const PlanetaryBody>{} : std::span<const PlanetaryBody>{found->second, 1};
    }
private:
    static std::uint64_t key(int system, int body) {
        return (std::uint64_t{static_cast<std::uint32_t>(system)} << 32) | static_cast<std::uint32_t>(body);
    }
    std::unordered_map<std::uint64_t, const PlanetaryBody*> bodies_;
};
} // namespace stellar::core
