#pragma once

namespace stellar::core {
class AdaptiveResearchCivilizationState;
class AdaptiveResearchForeignTechnologyRuntime;
class AdaptiveResearchForeignTechnologyState;

namespace detail {

class AdaptiveResearchForeignTechnologySupportAccess final {
public:
  [[nodiscard]] static AdaptiveResearchForeignTechnologyState &
  get_or_create(const AdaptiveResearchForeignTechnologyRuntime &runtime,
                const AdaptiveResearchCivilizationState &civilization);
};

} // namespace detail
} // namespace stellar::core
