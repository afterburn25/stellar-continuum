#pragma once

#include <stellar/core/civilization_catalog.hpp>

#include <vector>

namespace stellar::core::detail {

[[nodiscard]] std::vector<CivilizationOffice>
civilization_founding_roster(int civilization_id, bool human);

} // namespace stellar::core::detail