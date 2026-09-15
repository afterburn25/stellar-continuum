#pragma once

namespace stellar::core {
class AdaptiveResearchCivilizationState;
class AdaptiveResearchOutcomeRuntime;
class AdaptiveResearchOutcomeState;

namespace detail {

class AdaptiveResearchOutcomeSupportAccess final {
public:
  [[nodiscard]] static AdaptiveResearchOutcomeState &
  state(const AdaptiveResearchOutcomeRuntime &runtime,
        const AdaptiveResearchCivilizationState &civilization);
};

} // namespace detail
} // namespace stellar::core
