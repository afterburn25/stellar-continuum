#pragma once

namespace stellar::core {
class AdaptiveResearchCivilizationState;
class AdaptiveResearchPressureRuntime;
class AdaptiveResearchPressureState;

namespace detail {

class AdaptiveResearchPressureSupportAccess final {
public:
  [[nodiscard]] static AdaptiveResearchPressureState &
  state(const AdaptiveResearchPressureRuntime &runtime,
        const AdaptiveResearchCivilizationState &civilization);
};

} // namespace detail
} // namespace stellar::core
