#pragma once

#include <string>

namespace stellar::engine {

// One {resource id, quantity} pair used across the specialization
// frameworks (colony costs/demands, economy catalog recipes). A single
// definition keeps every framework header composable — previously the
// catalog declared a struct while colony aliased std::pair, so any
// translation unit including both could not compile.
struct ResourceAmount {
    std::string resource;
    double amount{0.0};
};

} // namespace stellar::engine
