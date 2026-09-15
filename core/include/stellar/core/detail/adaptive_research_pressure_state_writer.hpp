#pragma once

#include <stellar/core/adaptive_research_pressure.hpp>

#include <string>
#include <string_view>

namespace stellar::core::detail {

// Internal mutation seam for pressure runtime and future snapshot restoration.
class AdaptiveResearchPressureStateWriter final {
public:
  static bool set_metric_signal(AdaptiveResearchPressureState &state,
                                std::string signal_id, double normalized_value);
  static bool activate_pressure(AdaptiveResearchPressureState &state,
                                std::string pressure_id);
  static bool deactivate_pressure(AdaptiveResearchPressureState &state,
                                  std::string_view pressure_id);
};

} // namespace stellar::core::detail
