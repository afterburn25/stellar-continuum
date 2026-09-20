#pragma once

#include <stellar/core/civilization_catalog.hpp>
#include <functional>

namespace stellar::core {

// Control is independent of identity/ownership. Borrowed simulation views can
// supply a policy; existing callers retain ordinary human-vs-AI behavior.
using CivilizationControlQuery = std::function<bool(int civilization_id)>;
[[nodiscard]] inline bool civilization_uses_ai(
    const Civilization &civilization, const CivilizationControlQuery &control = {}) {
  return control ? control(civilization.id) : !civilization.is_player;
}
}
