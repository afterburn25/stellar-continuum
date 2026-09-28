#pragma once
#include <stellar/engine/native_map_platform.hpp>

namespace stellar::native_map {
// Keep distant stars equally unobtrusive when entering a system from the map.
// This is a display tint; approved source pixels and their detail stay intact.
inline constexpr Color faint_starfield_tint{148,148,148,255};
}
