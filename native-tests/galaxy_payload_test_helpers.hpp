#pragma once

// Reuse the already source-proven lower-gate DTO parsers and projections in
// isolated namespaces. All dependencies are included globally first so the
// included test implementations cannot open a nested std namespace.
#include <algorithm>
#include <array>
#include <bit>
#include <cctype>
#include <cmath>
#include <cstdint>
#include <exception>
#include <filesystem>
#include <fstream>
#include <functional>
#include <iomanip>
#include <iostream>
#include <iterator>
#include <limits>
#include <map>
#include <numeric>
#include <optional>
#include <ranges>
#include <set>
#include <span>
#include <sstream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <typeinfo>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <variant>
#include <vector>

#include <nlohmann/json.hpp>

#include <stellar/core/campaign_foundation_persistence.hpp>
#include <stellar/core/detail/adaptive_research_sha256.hpp>
#include <stellar/core/detail/adaptive_research_state_writer.hpp>
#include <stellar/core/fleet_combat_intelligence.hpp>
#include <stellar/core/fleet_persistence.hpp>
#include <stellar/core/fresh_campaign.hpp>
#include <stellar/core/galaxy_economy_persistence.hpp>
#include <stellar/core/galaxy_generation_metadata.hpp>
#include <stellar/core/knowledge_persistence.hpp>
#include <stellar/core/massive_combat_persistence.hpp>
#include <stellar/core/planetary_body_persistence.hpp>
#include <stellar/core/shipyard_persistence.hpp>

namespace gate087_foundation {
#define main gate087_foundation_unused_main
#include "campaign_foundation_persistence_tests.cpp"
#undef main
} // namespace gate087_foundation

namespace gate087_planetary {
#define main gate087_planetary_unused_main
#include "planetary_body_persistence_tests.cpp"
#undef main
} // namespace gate087_planetary

namespace gate087_economy {
#define main gate087_economy_unused_main
#include "galaxy_economy_persistence_tests.cpp"
#undef main
} // namespace gate087_economy

namespace gate087_fleet_state {
#define main gate087_fleet_state_unused_main
#include "fleet_state_tests.cpp"
#undef main
} // namespace gate087_fleet_state

namespace gate087_shipyard {
#define main gate087_shipyard_unused_main
#include "shipyard_persistence_tests.cpp"
#undef main
} // namespace gate087_shipyard

namespace gate087_knowledge {
#define main gate087_knowledge_unused_main
#include "knowledge_persistence_tests.cpp"
#undef main
} // namespace gate087_knowledge

namespace gate087_massive {
#define main gate087_massive_unused_main
#include "massive_combat_persistence_tests.cpp"
#undef main
} // namespace gate087_massive

namespace gate087_intelligence {
#define main gate087_intelligence_unused_main
#include "fleet_combat_intelligence_tests.cpp"
#undef main
} // namespace gate087_intelligence

namespace gate087_metadata {
#define main gate087_metadata_unused_main
#include "galaxy_generation_metadata_tests.cpp"
#undef main
} // namespace gate087_metadata
