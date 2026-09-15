#pragma once

// Preload every dependency globally so including the accepted Gate087 test
// implementation below cannot accidentally open `std` inside the isolation
// namespace.
#include "galaxy_payload_test_helpers.hpp"

#include <stellar/core/adaptive_research_strategic_runtime.hpp>
#include <stellar/core/detail/adaptive_research_sha256.hpp>
#include <stellar/core/galaxy_payload_persistence.hpp>
#include <stellar/core/integrated_adaptive_campaign.hpp>

#include <nlohmann/json.hpp>

#include <algorithm>
#include <cstdint>
#include <exception>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <optional>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <typeinfo>
#include <utility>

// Reuse the accepted current-format aggregate decoder and raw-state projections
// in one isolated namespace. Gate087's run/main are renamed and never invoked.
namespace gate090_current {
#define main gate090_current_unused_main
#include "galaxy_payload_persistence_tests.cpp"
#undef main
} // namespace gate090_current
